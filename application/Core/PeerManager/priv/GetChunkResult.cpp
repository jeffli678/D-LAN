/**
  * Aybabtu - A decentralized LAN file sharing software.
  * Copyright (C) 2010-2011 Greg Burri <greg.burri@gmail.com>
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */
  
#include <priv/GetChunkResult.h>
using namespace PM;

#include <Common/Settings.h>

#include <priv/Log.h>

GetChunkResult::GetChunkResult(const Protos::Core::GetChunk& chunk, QSharedPointer<Socket> socket)
   : IGetChunkResult(SETTINGS.get<quint32>("socket_timeout")), chunk(chunk), socket(socket), error(false)
{
}

GetChunkResult::~GetChunkResult()
{
   // We must disconnect because 'this->socket->finished' can read some data and emit 'newMessage'.
   disconnect(this->socket.data(), SIGNAL(newMessage(Common::Network::CoreMessageType, const google::protobuf::Message&)), this, SLOT(newMessage(Common::Network::CoreMessageType, const google::protobuf::Message&)));
   this->socket->finished(this->error || this->isTimeouted());
}

void GetChunkResult::start()
{
   connect(this->socket.data(), SIGNAL(newMessage(Common::Network::CoreMessageType, const google::protobuf::Message&)), this, SLOT(newMessage(Common::Network::CoreMessageType, const google::protobuf::Message&)), Qt::DirectConnection);
   socket->send(Common::Network::CORE_GET_CHUNK, this->chunk);
   this->startTimer();
}

void GetChunkResult::setError()
{
   this->error = true;
}

void GetChunkResult::newMessage(Common::Network::CoreMessageType type, const google::protobuf::Message& message)
{
   if (type != Common::Network::CORE_GET_CHUNK_RESULT)
      return;

   this->stopTimer();

   const Protos::Core::GetChunkResult& chunkResult = dynamic_cast<const Protos::Core::GetChunkResult&>(message);
   emit result(chunkResult);

   if (chunkResult.status() == Protos::Core::GetChunkResult_Status_OK)
   {
      emit stream(this->socket);
   }
   else
   {
      this->error = true;
      disconnect(this->socket.data(), SIGNAL(newMessage(Common::Network::CoreMessageType, const google::protobuf::Message&)), this, SLOT(newMessage(Common::Network::CoreMessageType, const google::protobuf::Message&)));
   }
}
