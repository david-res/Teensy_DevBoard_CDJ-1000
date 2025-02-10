#include "teensy41SQLite.hpp"

int T41SQLite::begin(FS* io_filesystem)
{
  m_filesystem = io_filesystem;
  return sqlite3_initialize();
}

int T41SQLite::end()
{
  int result = sqlite3_shutdown();
  m_filesystem = nullptr;
  return result;
}

FS* T41SQLite::getFilesystem()
{
  return m_filesystem;
}

void T41SQLite::setDBDirFullPath(const String &in_dbDirFullpath)
{
  m_dbDirFullpath = in_dbDirFullpath;
}

const String &T41SQLite::getDBDirFullPath() const
{
  return m_dbDirFullpath;
}

int T41SQLite::setLogCallback(LogCallback in_callback, void* in_forUseInCallback)
{
  return sqlite3_config(SQLITE_CONFIG_LOG, in_callback, in_forUseInCallback);
}

void T41SQLite::resetSectorSize()
{
  m_sectorSize = 0;
}

void T41SQLite::setSectorSize(int in_size)
{
  m_sectorSize = in_size;
}

int T41SQLite::getSectorSize() const
{
  return m_sectorSize;
}

/*
** This methods overwrites the values set with setDeviceCharacteristics!
** It returns true if a valid sector is given (set by call to setSectorSize).
** Otherwise it retruns false.
*/
bool T41SQLite::assumeSingleSectorWriteIsAtomic()
{
  switch (m_sectorSize)
  {
    case 512 * 1:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC512;
    break;

    case 512 * 2:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC1K;
    break;

    case 512 * 4:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC2K;
    break;

    case 512 * 8:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC4K;
    break;

    case 512 * 16:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC8K;
    break;

    case 512 * 32:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC16K;
    break;

    case 512 * 64:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC32K;
    break;

    case 512 * 128:
      m_deviceCharacteristics = SQLITE_IOCAP_ATOMIC64K;
    break;

    default:
      return false;
  }

  return true;
}

void T41SQLite::resetDeviceCharacteristics()
{
  m_deviceCharacteristics = 0;
}

void T41SQLite::setDeviceCharacteristics(int in_ioCap)
{
  m_deviceCharacteristics = in_ioCap;
}

int T41SQLite::getDeviceCharacteristics() const
{
  return m_deviceCharacteristics;
}
