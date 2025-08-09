#include <nan.h>
#include <iostream>
#include <windows.h>
#include <atomic>
#include <thread>

using namespace std;
using v8::Function;
using v8::Local;
using v8::Value;
using Nan::AsyncQueueWorker;
using Nan::AsyncProgressWorkerBase;
using Nan::Callback;
using Nan::HandleScope;
using Nan::New;
using Nan::Null;
using Nan::To;
using v8::Number;
using v8::FunctionTemplate;
using v8::Object;
using v8::String;
using Nan::GetFunction;
using Nan::Set;

HKEY hives[] = {
    HKEY_LOCAL_MACHINE,
    HKEY_CURRENT_USER,
    HKEY_CLASSES_ROOT,
    HKEY_USERS,
    HKEY_CURRENT_CONFIG
};

static std::string WinErrorToString(DWORD err) {
    if (err == ERROR_SUCCESS) return std::string();
    LPVOID msgBuf = nullptr;
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msgBuf, 0, NULL);
    std::string msg;
    if (size && msgBuf) {
        msg.assign((LPSTR)msgBuf, size);
        LocalFree(msgBuf);
    } else {
        msg = "Unknown Windows error " + to_string(err);
    }
    return msg;
}

class SetValueWorker : public Nan::AsyncWorker {
public:
    SetValueWorker(Callback* callback,
                   HKEY hive,
                   const std::string& path,
                   const std::string& valueName,
                   DWORD regType,
                   std::vector<BYTE>&& rawData)
        : Nan::AsyncWorker(callback),
          hive_(hive),
          path_(path),
          valueName_(valueName),
          regType_(regType),
          rawData_(std::move(rawData)) {}

    void Execute() override {
        HKEY hKey = NULL;
        LONG rc = RegCreateKeyExA(hive_, path_.c_str(), 0, NULL,
                                  REG_OPTION_NON_VOLATILE,
                                  KEY_SET_VALUE | KEY_WRITE,
                                  NULL, &hKey, NULL);
        if (rc != ERROR_SUCCESS) {
            SetErrorMessage(("RegCreateKeyExA failed: " + WinErrorToString(rc)).c_str());
            return;
        }
        rc = RegSetValueExA(hKey, valueName_.c_str(), 0, regType_,
                           rawData_.data(), (DWORD)rawData_.size());
        RegCloseKey(hKey);
        if (rc != ERROR_SUCCESS) {
            SetErrorMessage(("RegSetValueExA failed: " + WinErrorToString(rc)).c_str());
            return;
        }
    }

    void HandleOKCallback() override {
        HandleScope scope;
        v8::Local<v8::Value> argv[] = { Null(), Nan::New(true) };
        callback->Call(2, argv, async_resource);
    }

private:
    HKEY hive_;
    std::string path_;
    std::string valueName_;
    DWORD regType_;
    std::vector<BYTE> rawData_;
};

class RegistryWorker : public Nan::AsyncWorker {
public:
    RegistryWorker(Callback* callback, HKEY hive, const std::string& path, const std::string& valueName)
        : AsyncWorker(callback), registryHive(hive), registryPath(path), registryValueName(valueName) { }
    ~RegistryWorker() { }

    void Execute() override {
        HKEY hKey;
        const char* subKey = registryPath.c_str();
        const char* valueName = registryValueName.c_str();
        DWORD valueLength = 0;

        if (RegOpenKeyExA(registryHive, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            SetErrorMessage("Failed to open registry key.");
            return;
        }

        // Get required buffer size
        if (RegQueryValueExA(hKey, valueName, NULL, NULL, NULL, &valueLength) != ERROR_SUCCESS) {
            SetErrorMessage("Failed to read registry value size.");
            RegCloseKey(hKey);
            return;
        }

        std::vector<char> buffer(valueLength);
        if (RegQueryValueExA(hKey, valueName, NULL, NULL, reinterpret_cast<LPBYTE>(buffer.data()), &valueLength) != ERROR_SUCCESS) {
            SetErrorMessage("Failed to read registry value.");
            RegCloseKey(hKey);
            return;
        }

        resultValue.assign(buffer.begin(), buffer.end() - 1); // remove trailing null
        RegCloseKey(hKey);
    }

    void HandleOKCallback() override {
        HandleScope scope;
        Local<Value> argv[] = {
            Null(),
            New<String>(resultValue).ToLocalChecked()
        };
        callback->Call(2, argv, async_resource);
    }

private:
    HKEY registryHive;
    std::string registryPath;
    std::string registryValueName;
    std::string resultValue;
};

class RegistryWatchWorker : public AsyncProgressWorkerBase<char> {
public:
    RegistryWatchWorker(Callback* cb, HKEY hive, const std::string& path, const std::string& valueName)
        : AsyncProgressWorkerBase(cb), registryHive(hive), registryPath(path), registryValueName(valueName), stopFlag(false) {}

    ~RegistryWatchWorker() { stop(); }

    void Execute(const ExecutionProgress& progress) override {
        HKEY hKey;
        if (RegOpenKeyExA(registryHive, registryPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            SetErrorMessage("Failed to open registry key.");
            return;
        }

        while (!stopFlag) {
            // Wait for a change
            if (RegNotifyChangeKeyValue(hKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, NULL, FALSE) != ERROR_SUCCESS) {
                SetErrorMessage("Failed to set registry change notification.");
                break;
            }

            // Read new value
            DWORD valueLength = 0;
            if (RegQueryValueExA(hKey, registryValueName.c_str(), NULL, NULL, NULL, &valueLength) != ERROR_SUCCESS) {
                continue; // maybe deleted; skip until it exists again
            }
            std::vector<char> buffer(valueLength);
            if (RegQueryValueExA(hKey, registryValueName.c_str(), NULL, NULL, reinterpret_cast<LPBYTE>(buffer.data()), &valueLength) == ERROR_SUCCESS) {
                progress.Send(buffer.data(), valueLength - 1); // exclude null terminator
            }
        }
        RegCloseKey(hKey);
    }

    void HandleProgressCallback(const char* data, size_t size) override {
        HandleScope scope;
        Local<Value> argv[] = {
            Null(),
            New<String>(data, static_cast<int>(size)).ToLocalChecked()
        };
        callback->Call(2, argv, async_resource);
    }

    void stop() {
        stopFlag = true;
    }

private:
    HKEY registryHive;
    std::string registryPath;
    std::string registryValueName;
    std::atomic<bool> stopFlag;
};

NAN_METHOD(getValue) {
    if (info.Length() < 4) {
        Nan::ThrowTypeError("Wrong number of arguments");
        return;
    }
    if (!info[0]->IsNumber() || !info[1]->IsString() || !info[2]->IsString() || !info[3]->IsFunction()) {
        Nan::ThrowTypeError("Invalid arguments");
        return;
    }

    int hiveIndex = info[0]->Int32Value(Nan::GetCurrentContext()).FromJust();
    if (hiveIndex < 0 || hiveIndex >= sizeof(hives) / sizeof(HKEY)) {
        Nan::ThrowRangeError("Invalid hive index");
        return;
    }

    std::string registryPath = *Nan::Utf8String(info[1]);
    std::string valueName = *Nan::Utf8String(info[2]);
    Callback* callback = new Callback(To<Function>(info[3]).ToLocalChecked());

    Nan::AsyncQueueWorker(new RegistryWorker(callback, hives[hiveIndex], registryPath, valueName));
}

NAN_METHOD(watchValue) {
    if (info.Length() < 4) {
        Nan::ThrowTypeError("Wrong number of arguments");
        return;
    }
    if (!info[0]->IsNumber() || !info[1]->IsString() || !info[2]->IsString() || !info[3]->IsFunction()) {
        Nan::ThrowTypeError("Invalid arguments");
        return;
    }

    int hiveIndex = info[0]->Int32Value(Nan::GetCurrentContext()).FromJust();
    if (hiveIndex < 0 || hiveIndex >= sizeof(hives) / sizeof(HKEY)) {
        Nan::ThrowRangeError("Invalid hive index");
        return;
    }

    std::string registryPath = *Nan::Utf8String(info[1]);
    std::string valueName = *Nan::Utf8String(info[2]);
    Callback* callback = new Callback(To<Function>(info[3]).ToLocalChecked());

    Nan::AsyncQueueWorker(new RegistryWatchWorker(callback, hives[hiveIndex], registryPath, valueName));
}

NAN_METHOD(setValue) {
    if (info.Length() < 5) {
        return Nan::ThrowTypeError("Expected arguments: hiveIndex, path, valueName, value, callback");
    }
    if (!info[0]->IsNumber() || !info[1]->IsString() || !info[2]->IsString() || info[4]->IsUndefined() || !info[4]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument types");
    }

    int hiveIndex = Nan::To<int>(info[0]).FromJust();
    if (hiveIndex < 0 || hiveIndex >= sizeof(hives)/sizeof(hives[0])) {
        return Nan::ThrowRangeError("Invalid hive index");
    }
    std::string path = *Nan::Utf8String(info[1]);
    std::string valueName = *Nan::Utf8String(info[2]);
    v8::Local<v8::Value> val = info[3];
    Callback* callback = new Callback(info[4].As<v8::Function>());

    DWORD regType = REG_NONE;
    std::vector<BYTE> rawData;

    if (val->IsString()) {
        Nan::Utf8String utf8(val);
        std::string s(*utf8 ? *utf8 : "");
        rawData.assign(reinterpret_cast<const BYTE*>(s.c_str()), reinterpret_cast<const BYTE*>(s.c_str()) + s.size() + 1);
        regType = REG_SZ;
    }
    else if (val->IsBoolean()) {
        BOOL b = val->BooleanValue(Nan::GetCurrentContext()->GetIsolate());
        DWORD dw = b ? 1u : 0u;
        rawData.resize(sizeof(DWORD));
        memcpy(rawData.data(), &dw, sizeof(DWORD));
        regType = REG_DWORD;
    }
    else if (val->IsBigInt()) {
        v8::Local<v8::BigInt> bi = val.As<v8::BigInt>();
        bool lossless = false;
        int64_t asInt64 = bi->Int64Value(&lossless);
        if (lossless) {
            unsigned long long qw = static_cast<unsigned long long>(asInt64);
            rawData.resize(sizeof(ULONGLONG));
            memcpy(rawData.data(), &qw, sizeof(ULONGLONG));
            regType = REG_QWORD;
        } else {
            v8::Local<v8::String> s = bi->ToString(Nan::GetCurrentContext()).ToLocalChecked();
            Nan::Utf8String utf8(s);
            std::string str(*utf8 ? *utf8 : "");
            rawData.assign(reinterpret_cast<const BYTE*>(str.c_str()), reinterpret_cast<const BYTE*>(str.c_str()) + str.size() + 1);
            regType = REG_SZ;
        }
    }
    else if (val->IsNumber()) {
        double d = val->NumberValue(Nan::GetCurrentContext()).FromJust();
        double intpart;
        bool is_integer = modf(d, &intpart) == 0.0;
        if (is_integer) {
            if (d >= std::numeric_limits<uint32_t>::min() && d <= std::numeric_limits<uint32_t>::max()) {
                DWORD dw = static_cast<DWORD>(d);
                rawData.resize(sizeof(DWORD));
                memcpy(rawData.data(), &dw, sizeof(DWORD));
                regType = REG_DWORD;
            } else if (d >= (double)std::numeric_limits<int64_t>::min() && d <= (double)std::numeric_limits<int64_t>::max()) {
                long long ll = static_cast<long long>(d);
                unsigned long long qw = static_cast<unsigned long long>(ll);
                rawData.resize(sizeof(ULONGLONG));
                memcpy(rawData.data(), &qw, sizeof(ULONGLONG));
                regType = REG_QWORD;
            } else {
                v8::Local<v8::String> s = Nan::New<v8::Number>(d)->ToString(Nan::GetCurrentContext()).ToLocalChecked();
                Nan::Utf8String utf8(s);
                std::string str(*utf8 ? *utf8 : "");
                rawData.assign(reinterpret_cast<const BYTE*>(str.c_str()), reinterpret_cast<const BYTE*>(str.c_str()) + str.size() + 1);
                regType = REG_SZ;
            }
        } else {
            v8::Local<v8::String> s = Nan::New<v8::Number>(d)->ToString(Nan::GetCurrentContext()).ToLocalChecked();
            Nan::Utf8String utf8(s);
            std::string str(*utf8 ? *utf8 : "");
            rawData.assign(reinterpret_cast<const BYTE*>(str.c_str()), reinterpret_cast<const BYTE*>(str.c_str()) + str.size() + 1);
            regType = REG_SZ;
        }
    }
    else {
        delete callback;
        return Nan::ThrowError("Unsupported value type for registry");
    }

    Nan::AsyncQueueWorker(new SetValueWorker(callback, hives[hiveIndex], path, valueName, regType, std::move(rawData)));
}

NAN_MODULE_INIT(InitAll) {
    Set(target, New<String>("getValue").ToLocalChecked(), GetFunction(New<FunctionTemplate>(getValue)).ToLocalChecked());
    Set(target, New<String>("watchValue").ToLocalChecked(), GetFunction(New<FunctionTemplate>(watchValue)).ToLocalChecked());
    Set(target, New<String>("setValue").ToLocalChecked(), GetFunction(New<FunctionTemplate>(setValue)).ToLocalChecked());

    Set(target, New<String>("HKLM").ToLocalChecked(), New<Number>(0));
    Set(target, New<String>("HKCU").ToLocalChecked(), New<Number>(1));
    Set(target, New<String>("HKCR").ToLocalChecked(), New<Number>(2));
    Set(target, New<String>("HKU").ToLocalChecked(), New<Number>(3));
    Set(target, New<String>("HKCC").ToLocalChecked(), New<Number>(4));
}

NODE_MODULE(addon, InitAll);