#include "registry.h"
#include "convert.h"

#include <iostream>
#include <windows.h>
#include <nan.h>
#include <vector>
#include <string>

std::vector<BYTE> ReadValue( HKEY hive, const std::string& path, const std::string& valueName, DWORD& regType ) {
	HKEY hKey;
	DWORD valueLength = 0;

	if( RegOpenKeyExA( hive, path.c_str( ), 0, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to open registry key." );
	}

	auto lPath = std::wstring( valueName.begin( ), valueName.end( ) ).c_str( );

	if( RegQueryValueExW( hKey, lPath, NULL, &regType, NULL, &valueLength ) != ERROR_SUCCESS ) {
		RegCloseKey( hKey );
		throw std::runtime_error( "Failed to read registry value size." );
	}

	std::vector<BYTE> buffer( valueLength );
	if( RegQueryValueExW( hKey, lPath, NULL, &regType, buffer.data( ), &valueLength ) != ERROR_SUCCESS ) {
		RegCloseKey( hKey );
		throw std::runtime_error( "Failed to read registry value." );
	}

	RegCloseKey( hKey );
	return buffer;
}

bool SetValue( HKEY hive, const std::string& path, const std::string& valueName, DWORD regType, const std::vector<BYTE>& rawData ) {
	HKEY hKey;
	if( RegCreateKeyExW( hive, std::wstring( path.begin( ), path.end( ) ).c_str( ), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WRITE, NULL, &hKey, NULL ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to create registry key." );
	}

	if( RegSetValueExW( hKey, std::wstring( valueName.begin( ), valueName.end( ) ).c_str( ), 0, regType, rawData.data( ), ( DWORD ) rawData.size( ) ) != ERROR_SUCCESS ) {
		RegCloseKey( hKey );
		throw std::runtime_error( "Failed to set registry key value." );
	}

	RegCloseKey( hKey );
	return true;
}

std::vector<std::wstring> EnumerateKeys( HKEY hive, const std::string& path ) {
	HKEY hKey;
	if( RegOpenKeyExA( hive, path.c_str( ), 0, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to open registry key." );
	}

	std::vector<std::wstring> result;

	DWORD index = 0;
	WCHAR name[ 256 ];
	DWORD nameLength = sizeof( name ) / sizeof( WCHAR );

	while( RegEnumKeyExW( hKey, index, name, &nameLength, NULL, NULL, NULL, NULL ) == ERROR_SUCCESS ) {
		result.emplace_back( name, name + nameLength );
		index++;
		nameLength = sizeof( name ) / sizeof( WCHAR );
	}

	RegCloseKey( hKey );
	return result;
}

std::vector<std::pair<std::wstring, RegistryData>> EnumerateValues( HKEY hive, const std::string& path ) {
	HKEY hKey;
	if( RegOpenKeyExA( hive, path.c_str( ), 0, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to open registry key." );
	}

	std::vector<std::pair<std::wstring, RegistryData>> values;

	DWORD index = 0;
	WCHAR name[ 256 ];
	DWORD nameLength;
	DWORD type;
	DWORD dataLength;

	while( true ) {
		nameLength = sizeof( name ) / sizeof( WCHAR );
		dataLength = 0;

		LONG ret = RegEnumValueW( hKey, index++, name, &nameLength, NULL, &type, NULL, &dataLength );
		if( ret == ERROR_NO_MORE_ITEMS ) {
			break;
		}

		if( ret != ERROR_SUCCESS ) {
			RegCloseKey( hKey );
			throw std::runtime_error( "Failed to enumerate registry values." );
		}

		std::vector<BYTE> data( dataLength );
		if( RegQueryValueExW( hKey, name, NULL, &type, data.data( ), &dataLength ) != ERROR_SUCCESS ) {
			RegCloseKey( hKey );
			throw std::runtime_error( "Failed to query registry value." );
		}

		values.emplace_back( std::wstring( name, nameLength ), RegistryData( type, data.data( ), dataLength ) );
	}

	RegCloseKey( hKey );
	return values;
}

bool DeleteValue( HKEY hive, const std::string& path, const std::string& valueName ) {
	HKEY hKey;
	if( RegOpenKeyExA( hive, path.c_str( ), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hKey ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to open registry key." );
	}

	if( RegDeleteKeyValueW( hKey, NULL, std::wstring( valueName.begin( ), valueName.end( ) ).c_str( ) ) != ERROR_SUCCESS ) {
		RegCloseKey( hKey );
		throw std::runtime_error( "Failed to delete key vlaue." );
	}

	RegCloseKey( hKey );
	return true;
}

bool DeleteKey( HKEY hive, const std::string& path ) {
	if( RegDeleteKeyExW( hive, std::wstring( path.begin( ), path.end( ) ).c_str( ), KEY_WOW64_64KEY, NULL ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to delete key." );
	}
	return true;
}

bool DeleteTree( HKEY hive, const std::string& path ) {
	HKEY hKey;
	if( RegOpenKeyExA( hive, path.c_str( ), 0, DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_WOW64_64KEY, &hKey ) != ERROR_SUCCESS ) {
		throw std::runtime_error( "Failed to open registry key." );
	}

	if( RegDeleteTreeW( hKey, NULL ) != ERROR_SUCCESS ) {
		RegCloseKey( hKey );
		throw std::runtime_error( "Failed to delete key tree." );
	}

	RegCloseKey( hKey );
	return true;
}

WatchWorker::WatchWorker( Nan::Callback* callback, HKEY hive, const std::string& path, const std::string& value ) : Nan::AsyncProgressWorkerBase<char>( callback ), hive( hive ), path( path ), value( value ), stopFlag( false ) { }
WatchWorker::~WatchWorker( ) {
	stop( );
}

void WatchWorker::Execute( const ExecutionProgress& progress ) {
	HKEY hKey;
	if( RegOpenKeyExA( hive, path.c_str( ), 0, KEY_READ, &hKey ) != ERROR_SUCCESS ) {
		SetErrorMessage( "Failed to open registry key." );
		return;
	}

	while( !stopFlag ) {
		if( RegNotifyChangeKeyValue( hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, NULL, FALSE ) != ERROR_SUCCESS ) {
			SetErrorMessage( "Failed to set registry change notification." );
			return;
		}

		DWORD type = 0, dataLength = 0;
		if( RegQueryValueExW( hKey, std::wstring( value.begin( ), value.end( ) ).c_str( ), NULL, &type, NULL, &dataLength ) != ERROR_SUCCESS ) {
			continue;
		}

		std::vector<BYTE> buffer( dataLength );
		if( RegQueryValueExW( hKey, std::wstring( value.begin( ), value.end( ) ).c_str( ), NULL, &type, buffer.data( ), &dataLength ) == ERROR_SUCCESS ) {
			size_t size = sizeof( DWORD ) + dataLength;
			std::vector<char> payload( size );
			memcpy( payload.data( ), &type, sizeof( DWORD ) );
			memcpy( payload.data( ) + sizeof( DWORD ), buffer.data( ), dataLength );
			progress.Send( payload.data( ), size );
		}
	}

	RegCloseKey( hKey );
}

void WatchWorker::HandleProgressCallback( const char* data, size_t size ) {
	Nan::HandleScope scope;
	if( size < sizeof( DWORD ) ) return;
	DWORD type;
	memcpy( &type, data, sizeof( DWORD ) );
	const BYTE* valueData = reinterpret_cast<const BYTE*>( data + sizeof( DWORD ) );
	DWORD valueSize = static_cast<DWORD>( size - sizeof( DWORD ) );

	v8::Local<v8::Value> jsValue = ConvertRegistryValue( type, valueData, valueSize );
	v8::Local<v8::Value> argv[ ] = { Nan::Null( ), jsValue };
	callback->Call( 2, argv, async_resource );
}

void WatchWorker::stop( ) {
	stopFlag = true;
}