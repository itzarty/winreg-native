#define _WINSOCKAPI_
#pragma once

#include <iostream>
#include <windows.h>
#include <nan.h>
#include <vector>
#include <atomic>
#include <string>

#include "convert.h"

std::vector<BYTE> ReadValue( HKEY hive, const std::string& path, const std::string& valueName, DWORD& regType );
bool SetValue( HKEY hive, const std::string& path, const std::string& valueName, DWORD regType, const std::vector<BYTE>& rawData );
std::vector<std::wstring> EnumerateKeys( HKEY hive, const std::string& path );
std::vector<std::pair<std::wstring, RegistryData>> EnumerateValues( HKEY hive, const std::string& path );
bool DeleteValue( HKEY hive, const std::string& path, const std::string& valueName );
bool DeleteKey( HKEY hive, const std::string& path );
bool DeleteTree( HKEY hive, const std::string& path );

class WatchWorker : public Nan::AsyncProgressWorkerBase<char> {
public:
	WatchWorker( Nan::Callback* callback, HKEY hive, const std::string& path, const std::string& value );
	~WatchWorker( );
	void Execute( const ExecutionProgress& progress ) override;
	void HandleProgressCallback( const char* data, size_t size ) override;
	void stop( );

private:
	HKEY hive;
	std::string path;
	std::string value;
	std::atomic<bool> stopFlag;
};