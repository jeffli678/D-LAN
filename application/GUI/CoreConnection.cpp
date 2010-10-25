#include "CoreConnection.h"
using namespace GUI;

#include <QHostAddress>

#include <Common/ZeroCopyStreamQIODevice.h>
#include <Common/Settings.h>
#include <Common/ProtoHelper.h>

BrowseResult::BrowseResult(CoreConnection* coreConnection, const Common::Hash& peerID)
   : peerID(peerID), tag(0)
{
   this->init(coreConnection);
}

BrowseResult::BrowseResult(CoreConnection* coreConnection, const Common::Hash& peerID, const Protos::Common::Entry& entry)
   : peerID(peerID), entry(entry), tag(0)
{
   this->init(coreConnection);
}

void BrowseResult::start()
{
   Protos::GUI::Browse browseMessage;
   browseMessage.mutable_peer_id()->set_hash(this->peerID.getData(), Common::Hash::HASH_SIZE);
   if (this->entry.IsInitialized())
      browseMessage.mutable_dir()->CopyFrom(this->entry);

   this->coreConnection->send(0x41, browseMessage);
}

void BrowseResult::setTag(quint64 tag)
{
   this->tag = tag;
}

void BrowseResult::browseResult(quint64 tag, const Protos::Common::Entries& entries)
{
   if (tag == this->tag) // Is this message for us?
   {
      this->tag = 0; // To avoid multi emit (should not occurs).
      emit result(entries);
   }
}

void BrowseResult::init(CoreConnection* coreConnection)
{
   this->coreConnection = coreConnection;
   connect(this->coreConnection, SIGNAL(browseResult(quint64, const Protos::Common::Entries&)), this, SLOT(browseResult(quint64, const Protos::Common::Entries&)));
}

/////

CoreConnection::CoreConnection()
{
   connect(&this->socket, SIGNAL(readyRead()), this, SLOT(dataReceived()));
   connect(&this->socket, SIGNAL(connected()), this, SIGNAL(coreConnected()));
   connect(&this->socket, SIGNAL(disconnected()), this, SIGNAL(coreDisconnected()));
}

void CoreConnection::connectToCore()
{
   QHostAddress address(SETTINGS.get<QString>("core_address"));

   // If the address is local check if the core is launched, if not try to launch it.
   if (address == QHostAddress::LocalHost)
   {
   }

   this->socket.connectToHost(SETTINGS.get<QString>("core_address"), SETTINGS.get<quint32>("core_port"));
}

Common::Hash CoreConnection::getOurID() const
{
   return this->ourID;
}

void CoreConnection::sendChatMessage(const QString& message)
{
   Protos::GUI::ChatMessage chatMessage;
   Common::ProtoHelper::setStr(chatMessage, &Protos::GUI::ChatMessage::set_message, message);
   this->send(0x81, chatMessage);
}

void CoreConnection::setCoreSettings(const Protos::GUI::CoreSettings settings)
{
   this->send(0x21, settings);
}

QSharedPointer<BrowseResult> CoreConnection::browse(const Common::Hash& peerID)
{
   QSharedPointer<BrowseResult> browseResult = QSharedPointer<BrowseResult>(new BrowseResult(this, peerID));
   this->browseResultsWithoutTag << browseResult;
   return browseResult;
}

QSharedPointer<BrowseResult> CoreConnection::browse(const Common::Hash& peerID, const Protos::Common::Entry& entry)
{
   QSharedPointer<BrowseResult> browseResult = QSharedPointer<BrowseResult>(new BrowseResult(this, peerID, entry));
   this->browseResultsWithoutTag << browseResult;
   return browseResult;
}

void CoreConnection::send(quint32 type, const google::protobuf::Message& message)
{
   Common::MessageHeader header(type, message.ByteSize(), this->ourID);

   //L_DEBU(QString("CoreConnection::send : header.type = %1, header.size = %2\n%3").arg(header.type, 0, 16).arg(header.size).arg(Common::ProtoHelper::getDebugStr(message)));

   Common::Network::writeHeader(this->socket, header);
   Common::ZeroCopyOutputStreamQIODevice outputStream(&this->socket);
   message.SerializeToZeroCopyStream(&outputStream);
   /*if (!message.SerializeToZeroCopyStream(&outputStream))
      L_WARN(QString("Unable to send %1").arg(Common::ProtoHelper::getDebugStr(message)));*/
}

void CoreConnection::dataReceived()
{
   // TODO : it will loop infinetly if not enough data is provided.
   /*while (!this->socket.atEnd())
   {*/
      if (this->currentHeader.isNull() && this->socket.bytesAvailable() >= Common::Network::HEADER_SIZE)
      {
         this->currentHeader = Common::Network::readHeader(this->socket);
         this->ourID = this->currentHeader.senderID;
      }

      if (!this->currentHeader.isNull() && this->socket.bytesAvailable() >= this->currentHeader.size)
      {
         this->readMessage();
         this->currentHeader.setNull();
      }
   //}
}

bool CoreConnection::readMessage()
{
   bool readOK = false;

   switch (this->currentHeader.type)
   {
   case 0x01 : // State.
      {
         Protos::GUI::State state;

         // This scope (and the others ones below) is here to force the input stream to read all the bytes.
         // See Common::ZeroCopyInputStreamQIODevice::~ZeroCopyInputStreamQIODevice.
         {
            Common::ZeroCopyInputStreamQIODevice inputStream(&this->socket);
            readOK = state.ParseFromBoundedZeroCopyStream(&inputStream, this->currentHeader.size);
         }

         if (readOK)
            emit newState(state);
      }
      break;

   case 0x11 : // EventChatMessage.
      {
         Protos::GUI::EventChatMessage eventChatMessage;

         // This scope (and the others ones below) is here to force the input stream to read all the bytes.
         // See Common::ZeroCopyInputStreamQIODevice::~ZeroCopyInputStreamQIODevice.
         {
            Common::ZeroCopyInputStreamQIODevice inputStream(&this->socket);
            readOK = eventChatMessage.ParseFromBoundedZeroCopyStream(&inputStream, this->currentHeader.size);
         }

         if (readOK)
         {
            Common::Hash peerID(eventChatMessage.peer_id().hash().data());
            emit newChatMessage(peerID, Common::ProtoHelper::getStr(eventChatMessage, &Protos::GUI::EventChatMessage::message));
         }
      }
      break;

   case 0x42 : // Tag (for Browse).
      {
         Protos::GUI::Tag tagMessage;

         // This scope (and the others ones below) is here to force the input stream to read all the bytes.
         // See Common::ZeroCopyInputStreamQIODevice::~ZeroCopyInputStreamQIODevice.
         {
            Common::ZeroCopyInputStreamQIODevice inputStream(&this->socket);
            readOK = tagMessage.ParseFromBoundedZeroCopyStream(&inputStream, this->currentHeader.size);
         }

         if (readOK && !browseResultsWithoutTag.isEmpty())
         {
            this->browseResultsWithoutTag.takeFirst()->setTag(tagMessage.tag());
         }
      }
      break;

   case 0x43 : // BrowseResult.
      {
         Protos::GUI::BrowseResult browseResultMessage;

         // This scope (and the others ones below) is here to force the input stream to read all the bytes.
         // See Common::ZeroCopyInputStreamQIODevice::~ZeroCopyInputStreamQIODevice.
         {
            Common::ZeroCopyInputStreamQIODevice inputStream(&this->socket);
            readOK = browseResultMessage.ParseFromBoundedZeroCopyStream(&inputStream, this->currentHeader.size);
         }

         if (readOK)
         {
            emit browseResult(browseResultMessage.tag(), browseResultMessage.entries());
         }
      }
      break;

   default:
      readOK = false;
   }

   return readOK;
}