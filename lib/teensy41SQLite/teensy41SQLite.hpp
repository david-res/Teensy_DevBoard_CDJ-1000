#ifndef TEENSY_41_SQLITE
#define TEENSY_41_SQLITE

#include "sqlite3.h"

#include <FS.h>

class T41SQLite
{
  public:
    using LogCallback = void (*)(void* pArg, int iErrCode, const char* zMsg);

  public:
    static const int IS_DEFAULT_VFS = 1;
    static const int ACCESS_FAILED = 0;
    static const int ACCESS_SUCCESFUL = 1;
    
  private:
    int m_sectorSize = 0;
    int m_deviceCharacteristics = 0;
    FS* m_filesystem = nullptr;
    String m_dbDirFullpath = "/";

  private:
    T41SQLite() = default;
    ~T41SQLite() = default;

  public:
    T41SQLite(const T41SQLite&) = delete;
    T41SQLite& operator=(const T41SQLite&) = delete;

    static T41SQLite& getInstance()
    {
      static T41SQLite instance;
      return instance;
    }

    int begin(FS* io_filesystem);
    int end();
    
    FS* getFilesystem();
    
    void setDBDirFullPath(const String& in_dbDirFullpath);
    const String& getDBDirFullPath() const;

    int setLogCallback(LogCallback in_callback, void* in_forUseInCallback = nullptr);

    void resetSectorSize();
    void setSectorSize(int in_size);
    int getSectorSize() const;

    bool assumeSingleSectorWriteIsAtomic();
    void resetDeviceCharacteristics();
    void setDeviceCharacteristics(int in_ioCap);
    int getDeviceCharacteristics() const;
};

//#define TEENSY_41_SQLITE_DEBUG

#ifdef TEENSY_41_SQLITE_DEBUG
  #define TEENSY_41_SQLITE_DEBUG_SERIAL_PRINT(...) Serial.print(__VA_ARGS__)
  #define TEENSY_41_SQLITE_DEBUG_SERIAL_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define TEENSY_41_SQLITE_DEBUG_SERIAL_PRINT(...)
  #define TEENSY_41_SQLITE_DEBUG_SERIAL_PRINTLN(...)
#endif // TEENSY_41_SQLITE_DEBUG

#endif // TEENSY_41_SQLITE
