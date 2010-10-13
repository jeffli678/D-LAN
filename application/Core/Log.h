#ifndef CORE_LOG_H
#define CORE_LOG_H

#include <Common/LogManager/Builder.h>
#include <Common/LogManager/ILogger.h>

namespace Core
{
   static QSharedPointer<LM::ILogger> logger(LM::Builder::newLogger("Core"));

   #define L_USER(mess) LOG_USER(logger, mess)
   #define L_DEBU(mess) LOG_DEBU(logger, mess)
   #define L_WARN(mess) LOG_WARN(logger, mess)
   #define L_ERRO(mess) LOG_ERRO(logger, mess)
   #define L_FATA(mess) LOG_FATA(logger, mess)
}

#endif
