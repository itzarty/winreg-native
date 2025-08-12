#define NOMINMAX // Electron-rebuild fix
#include <nan.h>
#include <iostream>
#include <windows.h>

#include "async.h"
#include "convert.h"
#include "registry.h"

HKEY Hives[ ] = {
	HKEY_LOCAL_MACHINE,
	HKEY_CURRENT_USER,
	HKEY_CLASSES_ROOT,
	HKEY_USERS,
	HKEY_CURRENT_CONFIG
};

static HKEY validateArgs( const Nan::FunctionCallbackInfo<v8::Value>& info, int args ) {
	int total = args + 1;
	if( info.Length( ) < total ) {
		Nan::ThrowRangeError( "Invalid arguments" );
		return nullptr;
	}

	if( !info[ 0 ]->IsNumber( ) ) {
		Nan::ThrowRangeError( "Invalid hive" );
		return nullptr;
	}

	int hiveIndex = Nan::To<int32_t>( info[ 0 ] ).FromJust( );
	size_t hiveCount = sizeof( Hives ) / sizeof( HKEY );
	if( hiveIndex < 0 || static_cast<size_t>( hiveIndex ) >= hiveCount ) {
		Nan::ThrowRangeError( "Invalid hive" );
		return nullptr;
	}

	return Hives[ hiveIndex ];
}


NAN_METHOD( enumerateValues ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );

	RunAsync<std::vector<std::pair<std::wstring, RegistryData>>>( info, [=]( ) {
		return EnumerateValues( hive, path );
	}, [ ]( const std::vector<std::pair<std::wstring, RegistryData>>& result ) {
		return ConvertValues( result );
	} );
}

NAN_METHOD( enumerateKeys ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );

	RunAsync<std::vector<std::wstring>>( info, [=]( ) {
		return EnumerateKeys( hive, path );
	}, [ ]( const std::vector<std::wstring>& result ) {
		return ConvertSubkeys( result );
	} );
}

NAN_METHOD( getValue ) {
	HKEY hive = validateArgs( info, 2 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );

	RunAsync<std::pair<DWORD, std::vector<BYTE>>>( info, [=]( ) {
		DWORD type;
		auto data = ReadValue( hive, path, value, type );
		return std::make_pair( type, data );
	},
	[ ]( const std::pair<DWORD, std::vector<BYTE>>& result ) {
		return ConvertRegistryValue( result.first, result.second.data( ), ( DWORD ) result.second.size( ) );
	} );
}

NAN_METHOD( setValue ) {
	HKEY hive = validateArgs( info, 3 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );
	v8::Local<v8::Value> data( info[ 3 ] );

	DWORD type = REG_NONE;
	std::vector<BYTE> rawData;

	try {
		ConvertJsValue( data, type, rawData );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
		return;
	}

	RunAsync<bool>( info, [=]( ) {
		return SetValue( hive, path, value, type, std::move( rawData ) );
	}, [ ]( bool result ) {
		return Nan::New( result );
	} );
}

NAN_METHOD( deleteValue ) {
	HKEY hive = validateArgs( info, 2 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );

	RunAsync<bool>( info, [=]( ) {
		return DeleteValue( hive, path, value );
	}, [ ]( bool result ) {
		return Nan::New( result );
	} );
}

NAN_METHOD( deleteKey ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );

	RunAsync<bool>( info, [=]( ) {
		return DeleteKey( hive, path );
	}, [ ]( bool result ) {
		return Nan::New( result );
	} );
}

NAN_METHOD( deleteTree ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );

	RunAsync<bool>( info, [=]( ) {
		return DeleteTree( hive, path );
	}, [ ]( bool result ) {
		return Nan::New( result );
	} );
}

// SYNC METHODS

NAN_METHOD( getValueSync ) {
	HKEY hive = validateArgs( info, 2 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );

	DWORD type;
	std::vector<BYTE> data;

	try {
		data = ReadValue( hive, path, value, type );
	} catch ( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
		return;
	}

	info.GetReturnValue( ).Set( ConvertRegistryValue( type, data.data( ), ( DWORD ) data.size( ) ) );
}

NAN_METHOD( setValueSync ) {
	HKEY hive = validateArgs( info, 3 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );
	v8::Local<v8::Value> data( info[ 3 ] );

	DWORD type = REG_NONE;
	std::vector<BYTE> rawData;
	bool result = false;

	try {
		ConvertJsValue( data, type, rawData );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
		return;
	}

	try {
		result = SetValue( hive, path, value, type, rawData );
	} catch ( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
		return;
	}

	info.GetReturnValue( ).Set( Nan::New( result ) );
}

NAN_METHOD( enumerateKeysSync ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );

	std::vector<std::wstring> entries;

	try {
		entries = EnumerateKeys( hive, path );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
		return;
	}

	info.GetReturnValue( ).Set( ConvertSubkeys( entries ) );
}

NAN_METHOD( enumerateValuesSync ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::vector<std::pair<std::wstring, RegistryData>> entries;

	try {
		entries = EnumerateValues( hive, path );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
		return;
	}

	v8::Local<v8::Object> result = ConvertValues( entries );
	info.GetReturnValue( ).Set( result );
}

NAN_METHOD( deleteValueSync ) {
	HKEY hive = validateArgs( info, 2 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );
	bool result = false;

	try {
		result = DeleteValue( hive, path, value );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
	}

	info.GetReturnValue( ).Set( Nan::New( result ) );
}

NAN_METHOD( deleteKeySync ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	bool result = false;

	try {
		result = DeleteKey( hive, path );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
	}

	info.GetReturnValue( ).Set( Nan::New( result ) );
}

NAN_METHOD( deleteTreeSync ) {
	HKEY hive = validateArgs( info, 1 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	bool result = false;

	try {
		result = DeleteTree( hive, path );
	} catch( const std::runtime_error& e ) {
		Nan::ThrowError( e.what( ) );
	}

	info.GetReturnValue( ).Set( Nan::New( result ) );
}

NAN_METHOD( watchValue ) {
	HKEY hive = validateArgs( info, 3 );
	if( !hive ) return;

	std::string path( *Nan::Utf8String( info[ 1 ] ) );
	std::string value( *Nan::Utf8String( info[ 2 ] ) );
	Nan::Callback* callback = new Nan::Callback( Nan::To<v8::Function>( info[ 3 ] ).ToLocalChecked( ) );

	Nan::AsyncQueueWorker( new WatchWorker( callback, hive, path, value ) );
}

void addMethod( auto target, auto name, auto method ) {
	Nan::Set( target, Nan::New<v8::String>( name ).ToLocalChecked( ), Nan::GetFunction( Nan::New<v8::FunctionTemplate>( method ) ).ToLocalChecked( ) );
}

NAN_MODULE_INIT( init ) {
	addMethod( target, "getValue", getValue );
	addMethod( target, "setValue", setValue );
	addMethod( target, "enumerateKeys", enumerateKeys );
	addMethod( target, "enumerateValues", enumerateValues );
	addMethod( target, "deleteValue", deleteValue );
	addMethod( target, "deleteKey", deleteKey );
	addMethod( target, "deleteTree", deleteTree );

	addMethod( target, "getValueSync", getValueSync );
	addMethod( target, "setValueSync", setValueSync );
	addMethod( target, "enumerateKeysSync", enumerateKeysSync );
	addMethod( target, "enumerateValuesSync", enumerateValuesSync );
	addMethod( target, "deleteValueSync", deleteValueSync );
	addMethod( target, "deleteKeySync", deleteKeySync );
	addMethod( target, "deleteTreeSync", deleteTreeSync );

	addMethod( target, "watchValue", watchValue );

	Nan::Set( target, Nan::New<v8::String>( "HKLM" ).ToLocalChecked( ), Nan::New<v8::Number>( 0 ) );
	Nan::Set( target, Nan::New<v8::String>( "HKCU" ).ToLocalChecked( ), Nan::New<v8::Number>( 1 ) );
	Nan::Set( target, Nan::New<v8::String>( "HKCR" ).ToLocalChecked( ), Nan::New<v8::Number>( 2 ) );
	Nan::Set( target, Nan::New<v8::String>( "HKU" ).ToLocalChecked( ), Nan::New<v8::Number>( 3 ) );
	Nan::Set( target, Nan::New<v8::String>( "HKCC" ).ToLocalChecked( ), Nan::New<v8::Number>( 4 ) );
}

NODE_MODULE( addon, init );