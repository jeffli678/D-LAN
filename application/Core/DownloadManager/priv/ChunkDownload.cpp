#include <priv/ChunkDownload.h>
using namespace DM;

#include <QElapsedTimer>

#include <Common/Settings.h>
#include <Core/FileManager/Exceptions.h>
#include <Core/PeerManager/IPeer.h>

#include <priv/Log.h>

/**
  * @class ChunkDownload
  * A class to download a file chunk. A ChunkDownload can exist only if we know its hash.
  * It can be created when a new FileDownload is added for each chunk known in the given entry or when a FileDownload receive a hash.
  */

ChunkDownload::ChunkDownload(QSharedPointer<PM::IPeerManager> peerManager, OccupiedPeers& occupiedPeersDownloadingChunk, Common::Hash chunkHash) :
   peerManager(peerManager),
   occupiedPeersDownloadingChunk(occupiedPeersDownloadingChunk),
   chunkHash(chunkHash),
   socket(0),
   downloading(false),
   networkError(false),
   mutex(QMutex::Recursive)
{
   L_DEBU(QString("New ChunkDownload : %1").arg(this->chunkHash.toStr()));
   connect(this, SIGNAL(finished()), this, SLOT(downloadingEnded()), Qt::QueuedConnection);
   this->mainThread = QThread::currentThread();
}

int ChunkDownload::getDownloadRate() const
{
   return this->transferRateCalculator.getTransferRate();
}

Common::Hash ChunkDownload::getHash()
{
   return this->chunkHash;
}

void ChunkDownload::addPeerID(const Common::Hash& peerID)
{
   QMutexLocker lock(&this->mutex);
   PM::IPeer* peer = this->peerManager->getPeer(peerID);
   if (peer && !this->peers.contains(peer))
   {
      this->peers << peer;
      this->occupiedPeersDownloadingChunk.newPeer(peer);
   }
}

void ChunkDownload::rmPeerID(const Common::Hash& peerID)
{
   QMutexLocker lock(&this->mutex);
   PM::IPeer* peer = this->peerManager->getPeer(peerID);
   if (peer)
      this->peers.removeOne(peer);
}

/* Old interface
void ChunkDownload::setPeerIDs(const QList<Common::Hash>& peerIDs)
{
   this->peers.clear();
   for (QListIterator<Common::Hash> i(peerIDs); i.hasNext();)
   {
      PM::IPeer* peer = this->peerManager->getPeer(i.next());
      if (peer)
      {
         this->peers << peer;
         this->occupiedPeersDownloadingChunk.newPeer(peer);
      }
   }
}*/

void ChunkDownload::setChunk(QSharedPointer<FM::IChunk> chunk)
{
   this->chunk = chunk;
   this->chunk->setHash(this->chunkHash);
}

QSharedPointer<FM::IChunk> ChunkDownload::getChunk() const
{
   return this->chunk;
}

void ChunkDownload::setPeerSource(PM::IPeer* peer, bool informOccupiedPeers)
{
   QMutexLocker lock(&this->mutex);
   if (!this->peers.contains(peer))
   {
      this->peers << peer;

      if (informOccupiedPeers)
         this->occupiedPeersDownloadingChunk.newPeer(peer);
   }
}

/**
  * To be ready :
  * - It must be have at least one peer.
  * - It isn't finished.
  * - It isn't currently downloading.
  */
bool ChunkDownload::isReadyToDownload()
{
   if (this->peers.isEmpty() || this->downloading || (!this->chunk.isNull() && this->chunk->isComplete()))
      return false;

   this->currentDownloadingPeer = this->getTheFastestFreePeer();

   return this->currentDownloadingPeer != 0;
}

bool ChunkDownload::isDownloading() const
{
   return this->downloading;
}

bool ChunkDownload::isComplete() const
{
   return !this->chunk.isNull() && this->chunk->isComplete();
}

bool ChunkDownload::hasAtLeastAPeer() const
{
   return !this->peers.isEmpty();
}

int ChunkDownload::getDownloadedBytes() const
{
   if (this->chunk.isNull())
      return 0;

   return this->chunk->getKnownBytes();
}

QList<Common::Hash> ChunkDownload::getPeers() const
{
   QList<Common::Hash> peerIDs;
   for (QListIterator<PM::IPeer*> i(this->peers); i.hasNext();)
      peerIDs << i.next()->getID();
   return peerIDs;
}

/**
  * Tell the chunkDownload to download the chunk from one of its peer.
  * @return true if the downloading has been started.
  */
bool ChunkDownload::startDownloading()
{
   if (this->chunk.isNull())
   {
      L_ERRO(QString("Unable to download without the chunk. Hash : %1").arg(this->chunkHash.toStr()));
      return false;
   }

   this->downloading = true;
   emit downloadStarted();

   this->occupiedPeersDownloadingChunk.setPeerAsOccupied(this->currentDownloadingPeer);

   Protos::Core::GetChunk getChunkMess;
   getChunkMess.mutable_chunk()->set_hash(this->chunkHash.getData(), Common::Hash::HASH_SIZE);
   getChunkMess.set_offset(this->chunk->getKnownBytes());
   this->getChunkResult = this->currentDownloadingPeer->getChunk(getChunkMess);
   connect(this->getChunkResult.data(), SIGNAL(result(const Protos::Core::GetChunkResult&)), this, SLOT(result(const Protos::Core::GetChunkResult&)));
   connect(this->getChunkResult.data(), SIGNAL(stream(PM::ISocket*)), this, SLOT(stream(PM::ISocket*)));

   this->getChunkResult->start();
   return true;
}

void ChunkDownload::run()
{
   try
   {
      L_DEBU(QString("Starting downloading a chunk : %1").arg(this->chunk->toStr()));

      QSharedPointer<FM::IDataWriter> writer = this->chunk->getDataWriter();

      const int BUFFER_SIZE = SETTINGS.get<quint32>("buffer_size");
      char buffer[BUFFER_SIZE];

      int bytesRead = 0;
      quint64 bytesReadTotal = 0;

      quint64 deltaRead = 0;

      this->transferRateCalculator.reset();

      QElapsedTimer timer;
      timer.start();

      forever
      {
         // Waiting for data..
         if (socket->getQSocket()->bytesAvailable() == 0 && !socket->getQSocket()->waitForReadyRead(SETTINGS.get<quint32>("timeout_during_transfer")))
         {
            L_WARN("Connection dropped");
            this->networkError = true;
            break;
         }

         bytesRead = socket->getQSocket()->read(buffer, BUFFER_SIZE);

         if (bytesRead == -1)
         {
            L_ERRO(QString("Socket : cannot receive data : %1").arg(this->chunk->toStr()));
            this->networkError = true;
            break;
         }

         bytesReadTotal += bytesRead;
         deltaRead += bytesRead;

         if (timer.elapsed() > 1000)
         {
            this->currentDownloadingPeer->setSpeed(deltaRead / timer.elapsed() * 1000);
            timer.start();
            deltaRead = 0;

            // If a another peer exists and our speed is lesser than 'lan_speed' / 'time_recheck_chunk_factor' and the other peer speed is greater than our
            // then we will try to switch to this peer.
            PM::IPeer* peer = this->getTheFastestFreePeer();
            if (
               peer && peer != this->currentDownloadingPeer &&
               this->currentDownloadingPeer->getSpeed() < SETTINGS.get<quint32>("lan_speed") / SETTINGS.get<quint32>("time_recheck_chunk_factor") &&
               peer->getSpeed() > SETTINGS.get<quint32>("switch_to_another_peer_factor") * this->currentDownloadingPeer->getSpeed()
            )
            {
               L_DEBU("Switch to a better peer..");
               break;
            }
         }

         writer->write(buffer, bytesRead);

         this->transferRateCalculator.addData(bytesRead);

         if (bytesReadTotal >= this->chunkSize)
            break;
      }
   }
   catch(FM::UnableToOpenFileInWriteModeException)
   {
      L_ERRO("UnableToOpenFileInWriteModeException");
   }
   catch(FM::IOErrorException&)
   {
      L_ERRO("IOErrorException");
   }
   catch (FM::ChunkDeletedException&)
   {
      L_ERRO("ChunkDeletedException");
   }
   catch (FM::TryToWriteBeyondTheEndOfChunkException&)
   {
      L_ERRO("TryToWriteBeyondTheEndOfChunkException");
   }

   this->socket->getQSocket()->moveToThread(this->mainThread);
   this->transferRateCalculator.reset();
}

void ChunkDownload::result(const Protos::Core::GetChunkResult& result)
{
   if (result.status() != Protos::Core::GetChunkResult_Status_OK)
   {
      L_WARN(QString("Status error from GetChunkResult : %1. Download aborted.").arg(result.status()));
      this->networkError = true;
      this->downloadingEnded();
   }
   else
   {
      if (!result.has_chunk_size())
      {
         L_ERRO(QString("Message 'GetChunkResult' doesn't contain the size of the chunk : %1. Download aborted.").arg(this->chunk->getHash().toStr()));
         this->networkError = true;
         this->downloadingEnded();
      }
      else
      {
         this->chunkSize = result.chunk_size();
      }
   }
}

void ChunkDownload::stream(PM::ISocket* socket)
{
   this->socket = socket;
   this->socket->stopListening();
   this->socket->getQSocket()->moveToThread(this);
   this->start();
}

void ChunkDownload::downloadingEnded()
{
   L_DEBU(QString("Downloading ended, chunk : %1").arg(this->chunk->toStr()));
   if (this->socket)
   {
      this->socket->getQSocket()->moveToThread(QThread::currentThread());
      this->socket->finished(this->networkError);
      this->socket = 0;
   }

   this->networkError = false;
   this->getChunkResult.clear();
   this->downloading = false;
   emit downloadFinished();

   // occupiedPeersDownloadingChunk can relaunch the download, so we have to set this->currentDownloadingPeer to 0 before.
   PM::IPeer* currentPeer = this->currentDownloadingPeer;
   this->currentDownloadingPeer = 0;
   this->occupiedPeersDownloadingChunk.setPeerAsFree(currentPeer);
}

PM::IPeer* ChunkDownload::getTheFastestFreePeer() const
{
   QMutexLocker lock(&this->mutex);

   PM::IPeer* current = 0;
   foreach (PM::IPeer* peer, this->peers)
   {
      if (this->occupiedPeersDownloadingChunk.isPeerFree(peer) && (!current || peer->getSpeed() > current->getSpeed()))
         current = peer;
   }
   return current;
}
