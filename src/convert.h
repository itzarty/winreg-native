#pragma once

#include <iostream>
#include <nan.h>
#include <windows.h>
#include <vector>

struct RegistryData {
	DWORD type;
	std::vector<BYTE> data;

	RegistryData( DWORD t, const BYTE* d, DWORD size ) : type( t ), data( d, d + size ) { }
};

void ConvertJsValue( v8::Local<v8::Value>& value, DWORD& type, std::vector<BYTE>& data );
v8::Local<v8::Value> ConvertRegistryValue( DWORD type, const BYTE* data, DWORD size );
v8::Local<v8::Array> ConvertSubkeys( const std::vector<std::wstring>& subkeys );
v8::Local<v8::Object> ConvertValues( const std::vector<std::pair<std::wstring, RegistryData>>& values );