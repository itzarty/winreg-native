#include "convert.h"

#include <iostream>
#include <nan.h>
#include <windows.h>

static std::wstring utf8_to_utf16( const std::string& utf8 ) {
	int size = MultiByteToWideChar( CP_UTF8, 0, utf8.data( ), ( int ) utf8.size( ), NULL, 0 );
	std::wstring wstr( size, 0 );
	MultiByteToWideChar( CP_UTF8, 0, utf8.data( ), ( int ) utf8.size( ), &wstr[ 0 ], size );
	return wstr;
}

void ConvertJsValue( v8::Local<v8::Value>& value, DWORD& type, std::vector<BYTE>& data ) {
	if( value->IsString( ) ) {
		Nan::Utf8String utf8( value );
		std::string s( *utf8 ? *utf8 : "" );
		std::wstring ws = utf8_to_utf16( s );
		data.assign( reinterpret_cast<const BYTE*>( ws.c_str( ) ), reinterpret_cast<const BYTE*>( ws.c_str( ) ) + ( ws.size( ) + 1 ) * sizeof( wchar_t ) );
		type = REG_SZ;
	} else if( value->IsBoolean( ) ) {
		BOOL b = value->BooleanValue( Nan::GetCurrentContext( )->GetIsolate( ) );
		DWORD dw = b ? 1u : 0u;
		data.resize( sizeof( DWORD ) );
		memcpy( data.data( ), &dw, sizeof( DWORD ) );
		type = REG_DWORD;
	} else if( value->IsBigInt( ) ) {
		v8::Local<v8::BigInt> bi = value.As<v8::BigInt>( );
		bool lossless = false;
		int64_t asInt64 = bi->Int64Value( &lossless );
		if( lossless ) {
			unsigned long long qw = static_cast<unsigned long long>( asInt64 );
			data.resize( sizeof( ULONGLONG ) );
			memcpy( data.data( ), &qw, sizeof( ULONGLONG ) );
			type = REG_QWORD;
		} else {
			v8::Local<v8::String> s = bi->ToString( Nan::GetCurrentContext( ) ).ToLocalChecked( );
			Nan::Utf8String utf8( s );
			std::string str( *utf8 ? *utf8 : "" );
			data.assign( reinterpret_cast<const BYTE*>( str.c_str( ) ), reinterpret_cast<const BYTE*>( str.c_str( ) ) + str.size( ) + 1 );
			type = REG_SZ;
		}
	}
	else if( value->IsNumber( ) ) {
		double d = value->NumberValue( Nan::GetCurrentContext( ) ).FromJust( );
		double intpart;
		bool is_integer = modf( d, &intpart ) == 0.0;
		if( is_integer ) {
			if( d >= std::numeric_limits<uint32_t>::min( ) && d <= std::numeric_limits<uint32_t>::max( ) ) {
				DWORD dw = static_cast<DWORD>( d );
				data.resize( sizeof( DWORD ) );
				memcpy( data.data( ), &dw, sizeof( DWORD ) );
				type = REG_DWORD;
			} else if( d >= ( double ) std::numeric_limits<int64_t>::min( ) && d <= ( double ) std::numeric_limits<int64_t>::max( ) ) {
				long long ll = static_cast<long long>( d );
				unsigned long long qw = static_cast<unsigned long long>( ll );
				data.resize( sizeof( ULONGLONG ) );
				memcpy( data.data( ), &qw, sizeof( ULONGLONG ) );
				type = REG_QWORD;
			} else {
				v8::Local<v8::String> s = Nan::New<v8::Number>( d )->ToString( Nan::GetCurrentContext( ) ).ToLocalChecked( );
				Nan::Utf8String utf8( s );
				std::string str( *utf8 ? *utf8 : "" );
				data.assign( reinterpret_cast<const BYTE*>( str.c_str( ) ), reinterpret_cast<const BYTE*>( str.c_str( ) ) + str.size( ) + 1 );
				type = REG_SZ;
			}
		} else {
			v8::Local<v8::String> s = Nan::New<v8::Number>( d )->ToString( Nan::GetCurrentContext( ) ).ToLocalChecked( );
			Nan::Utf8String utf8( s );
			std::string str( *utf8 ? *utf8 : "" );
			data.assign( reinterpret_cast<const BYTE*>( str.c_str( ) ), reinterpret_cast<const BYTE*>( str.c_str( ) ) + str.size( ) + 1 );
			type = REG_SZ;
		}
	} else {
		throw std::runtime_error( "Unsupported value type for registry." );
	}
}

v8::Local<v8::Value> ConvertRegistryValue( DWORD type, const BYTE* data, DWORD size ) {
	switch( type ) {
		case REG_DWORD: {
			if( size >= sizeof( DWORD ) ) {
				DWORD val;
				memcpy( &val, data, sizeof( DWORD ) );
				return Nan::New<v8::Number>( static_cast<uint32_t>( val ) );
			}
			break;
		}
		case REG_QWORD: {
			if( size >= sizeof( ULONGLONG ) ) {
				ULONGLONG val;
				memcpy( &val, data, sizeof( ULONGLONG ) );
				return v8::BigInt::New( Nan::GetCurrentContext( )->GetIsolate( ), static_cast<int64_t>( val ) );
			}
			break;
		}
		case REG_BINARY: {
			return Nan::CopyBuffer( reinterpret_cast<const char*>( data ), size ).ToLocalChecked( );
		}
		case REG_MULTI_SZ: {
			std::vector<v8::Local<v8::String>> strings;
			const wchar_t* wstr = reinterpret_cast<const wchar_t*>( data );
			size_t totalChars = size / sizeof( wchar_t );
			size_t i = 0;
			while( i < totalChars && wstr[ i ] != L'\0' ) {
				std::wstring ws( &wstr[ i ] );
				Nan::Utf8String utf8( Nan::New<v8::String>( reinterpret_cast<const uint16_t*>( ws.c_str( ) ), ws.size( ) ).ToLocalChecked( ) );
				strings.push_back( Nan::New<v8::String>( *utf8 ).ToLocalChecked( ) );
				i += ws.size( ) + 1;
			}
			v8::Local<v8::Array> arr = Nan::New<v8::Array>( strings.size( ) );
			for( uint32_t j = 0; j < strings.size( ); j++ ) {
				Nan::Set( arr, j, strings[ j ] );
			}
			return arr;
		}
		case REG_SZ:
		case REG_EXPAND_SZ: {
			const wchar_t* wstr = reinterpret_cast<const wchar_t*>( data );
			size_t len = wcsnlen( wstr, size / sizeof( wchar_t ) );
			return Nan::New<v8::String>( reinterpret_cast<const uint16_t*>( wstr ), static_cast<int>( len ) ).ToLocalChecked( );
		}
		default:
			return Nan::New<v8::String>( reinterpret_cast<const char*>( data ), static_cast<int>( size ) ).ToLocalChecked( );
	}
	return Nan::Null( );
}

v8::Local<v8::Array> ConvertSubkeys( const std::vector<std::wstring>& subkeys ) {
	v8::Isolate* isolate = v8::Isolate::GetCurrent( );
	v8::Local<v8::Array> result = v8::Array::New( isolate, static_cast<int>( subkeys.size( ) ) );
	for( int i = 0; i < subkeys.size( ); i++ ) {
		std::string utf8Key( subkeys[ i ].begin( ), subkeys[ i ].end( ) );
		Nan::Set( result, i, Nan::New( utf8Key ).ToLocalChecked( ) );
	}
	return result;
}

v8::Local<v8::Object> ConvertValues( const std::vector<std::pair<std::wstring, RegistryData>>& values ) {
	v8::Isolate* isolate = v8::Isolate::GetCurrent( );
	v8::Local<v8::Object> result = Nan::New<v8::Object>( );

	for( int i = 0; i < values.size( ); i++ ) {
		const auto& [ name, data ] = values[ i ];
		std::string nameUtf8( name.begin( ), name.end( ) );
		v8::Local<v8::Value> value = ConvertRegistryValue( data.type, data.data.data( ), static_cast<DWORD>( data.data.size( ) ) );
		Nan::Set( result, Nan::New( nameUtf8 ).ToLocalChecked( ), value );
	}
	return result;
}