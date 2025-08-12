#pragma once

#include <nan.h>

template<typename ResultType>
class AsyncWorker : public Nan::AsyncWorker {
public:
	using WorkFn = std::function<ResultType( )>;
	using ConvertFn = std::function<v8::Local<v8::Value>( const ResultType& )>;

	AsyncWorker( WorkFn workFn, ConvertFn convertFn, v8::Local<v8::Promise::Resolver> resolve ) : Nan::AsyncWorker( nullptr ), workFn( workFn ), convertFn( convertFn ) {
		resolver.Reset( resolve );
	}

	void Execute( ) override {
		try {
			result = workFn( );
		} catch( const std::runtime_error& error ) {
			SetErrorMessage( error.what( ) );
		}
	}

	void HandleOKCallback( ) override {
		Nan::HandleScope scope;
		auto localResolver = Nan::New( resolver );
		localResolver->Resolve( Nan::GetCurrentContext( ), convertFn( result ) ).Check( );
		resolver.Reset( );
	}

	void HandleErrorCallback( ) override {
		Nan::HandleScope scope;
		auto localResolver = Nan::New( resolver );
		localResolver->Reject( Nan::GetCurrentContext( ), Nan::Error( ErrorMessage( ) ) ).Check( );
		resolver.Reset( );
	}
private:
	WorkFn workFn;
	ConvertFn convertFn;
	ResultType result;
	Nan::Persistent<v8::Promise::Resolver> resolver;
};

template<typename ResultType>
void RunAsync( Nan::NAN_METHOD_ARGS_TYPE info, typename AsyncWorker<ResultType>::WorkFn workFn, typename AsyncWorker<ResultType>::ConvertFn convertFn ) {
	auto resolver = v8::Promise::Resolver::New( Nan::GetCurrentContext( ) ).ToLocalChecked( );
	auto* worker = new AsyncWorker<ResultType>( workFn, convertFn, resolver );
	Nan::AsyncQueueWorker( worker );
	info.GetReturnValue( ).Set( resolver->GetPromise( ) );
}