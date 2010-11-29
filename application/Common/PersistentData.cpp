#include <Common/PersistentData.h>
using namespace Common;

#include <QFile>
#include <QDir>
#include <QtDebug>

#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <Constants.h>
#include <Global.h>

/**
  * @class PersistentData
  * Some little functions to persist data and retrieve it.
  * The data are persisted in the user directory.
  * The data are described by a Protocol Buffer message.
  * Theses functions can be used for the application settings.
  */

const QString PersistentData::TEMP_SUFFIX_TERM(".temp");

/**
  * Define a value associated to a name.
  * You may refer to the name policy of the platform. Try to avoir special characters or space.
  * You can use an extension in the name like "settings.conf".
  * @exception PersistentDataIOException if the value can't be persisted.
  */
void PersistentData::setValue(const QString& name, const google::protobuf::Message& data, bool humanReadable)
{
   if (Global::createApplicationFolder())
   {
      const QString FILEPATH(APPLICATION_FOLDER_PATH + '/' + name);
      const QString TEMP_FILEPATH(FILEPATH + TEMP_SUFFIX_TERM);

      {
         QFile file(TEMP_FILEPATH);
         if (!file.open(QIODevice::WriteOnly))
            throw PersistentDataIOException(QString("Unable to open the file in write mode : %1, error : %2").arg(TEMP_FILEPATH).arg(file.errorString()));

#if !DEBUG
         if (humanReadable)
         {
#endif
            google::protobuf::io::FileOutputStream fileStream(file.handle());
            google::protobuf::TextFormat::Print(data, &fileStream);
#if !DEBUG
         }
         else
         {
            data.SerializeToFileDescriptor(file.handle());
         }
#endif
      }

      Common::Global::rename(TEMP_FILEPATH, FILEPATH);
   }
}

/**
  * Retrieve the data associated to a given name.
  * @exception UnknownValueException Throwed if the value doesn't exist
  */
void PersistentData::getValue(const QString& name, google::protobuf::Message& data, bool humanReadable)
{
   QFile file(APPLICATION_FOLDER_PATH + '/' + name);
   if (!file.open(QIODevice::ReadOnly))
      throw UnknownValueException();

#if !DEBUG
   if (humanReadable)
   {
#endif
      google::protobuf::io::FileInputStream fileStream(file.handle());
      google::protobuf::TextFormat::Parse(&fileStream, &data);
#if !DEBUG
   }
   else
   {
      data.ParsePartialFromFileDescriptor(file.handle());
   }
#endif
}

/**
  * Remove a data.
  * @return Return false if the data didn't exist.
  */
bool PersistentData::rmValue(const QString& name)
{
   return QFile::remove(APPLICATION_FOLDER_PATH + '/' + name);
}