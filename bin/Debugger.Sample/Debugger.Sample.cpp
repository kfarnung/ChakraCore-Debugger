// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "stdafx.h"

std::string GetMultiByteString(const wchar_t* str)
{
    size_t length = wcslen(str);
    if (length == 0)
    {
        return std::string();
    }

    DWORD result = WideCharToMultiByte(CP_UTF8, 0, str, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (result == 0)
    {
        return std::string();
    }

    std::vector<char> data(result);
    result = WideCharToMultiByte(CP_UTF8, 0, str, static_cast<int>(length), data.data(), static_cast<int>(data.size()), nullptr, nullptr);
    if (result == 0)
    {
        return std::string();
    }

    return std::string(begin(data), end(data));
}

std::wstring GetWideString(const char* str)
{
    size_t length = strlen(str);
    if (length == 0)
    {
        return std::wstring();
    }

    DWORD result = MultiByteToWideChar(CP_UTF8, 0, str, static_cast<int>(length), nullptr, 0);
    if (result == 0)
    {
        return std::wstring();
    }

    std::vector<wchar_t> data(result);
    result = MultiByteToWideChar(CP_UTF8, 0, str, static_cast<int>(length), data.data(), static_cast<int>(data.size()));
    if (result == 0)
    {
        return std::wstring();
    }

    return std::wstring(begin(data), end(data));
}

//
// Class to store information about command-line arguments to the host.
//
class CommandLineArguments
{
public:
    bool breakOnNextLine;
    bool enableDebugging;
    int port;
    bool help;
    bool serialize;

    std::vector<std::string> scriptArgs;

    CommandLineArguments()
        : breakOnNextLine(false)
        , enableDebugging(false)
        , port(9229)
        , help(false)
    {
    }

    void ParseCommandLine(int argc, wchar_t* argv[])
    {
        bool foundScript = false;

        for (int index = 1; index < argc; ++index)
        {
            std::string arg(GetMultiByteString(argv[index]));

            // Any flags before the script are considered host flags, anything else is passed to the script.
            if (!foundScript && (arg.length() > 0) && (arg[0] == L'-'))
            {
                if (!arg.compare("--inspect"))
                {
                    this->enableDebugging = true;
                }
                else if (!arg.compare("--inspect-brk"))
                {
                    this->enableDebugging = true;
                    this->breakOnNextLine = true;
                }
                else if (!arg.compare("--port") || !arg.compare("-p"))
                {
                    ++index;
                    if (argc > index)
                    {
                        // This will return zero if no number was found.
                        this->port = std::stoi(std::wstring(argv[index]));
                    }
                }
                else
                {
                    // Handle everything else including `-?` and `--help`
                    this->help = true;
                }
            }
            else
            {
                foundScript = true;

                // Collect any non-flag arguments
                this->scriptArgs.emplace_back(std::move(arg));
            }
        }

        if (this->port <= 0 || this->port > 65535 || this->scriptArgs.empty())
        {
            this->help = true;
        }
    }

    void ShowHelp()
    {
        fprintf(stderr,
            "\n"
            "Usage: ChakraCore.Debugger.Sample.exe [host-options] <script> [script-arguments]\n"
            "\n"
            "Options: \n"
            "      --inspect          Enable debugging\n"
            "      --inspect-brk      Enable debugging and break\n"
            "  -p, --port <number>    Specify the port number\n"
            "  -?  --help             Show this help info\n"
            "\n");
    }
};

//
// Source context counter.
//
unsigned currentSourceContext = 0;

//
// Helper to load a script from disk.
//
std::string LoadScript(const char* filename)
{
    std::ifstream ifs(filename, std::ifstream::in | std::ifstream::binary);

    if (!ifs.good())
    {
        fprintf(stderr, "chakrahost: unable to open file: %s.\n", filename);
        return std::string();
    }

    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

JsErrorCode RunScript(const char* filename, JsValueRef* result)
{
    wchar_t fullPath[MAX_PATH];
    std::wstring wFilename = GetWideString(filename);
    DWORD pathLength = GetFullPathName(wFilename.c_str(), MAX_PATH, fullPath, nullptr);
    if (pathLength > MAX_PATH || pathLength == 0)
    {
        return JsErrorInvalidArgument;
    }

    std::string filePath = GetMultiByteString(fullPath);

    // Load the script from the disk.
    std::string script = LoadScript(filePath.c_str());
    if (script.empty())
    {
        return JsErrorInvalidArgument;
    }

    JsValueRef scriptValue = nullptr;
    IfFailRet(JsCreateString(script.c_str(), script.length(), &scriptValue));

    JsValueRef sourceUrl = nullptr;
    IfFailRet(JsCreateString(filePath.c_str(), filePath.length(), &sourceUrl));

    // Run the script.
    IfFailRet(JsRun(scriptValue, currentSourceContext++, sourceUrl, JsParseScriptAttributeNone, result));

    return JsNoError;
}

//
// Callback to echo something to the command-line.
//
JsValueRef CALLBACK HostEcho(
    JsValueRef /*callee*/, 
    bool /*isConstructCall*/, 
    JsValueRef* arguments, 
    unsigned short argumentCount, 
    void* /*callbackState*/)
{
    for (unsigned int index = 1; index < argumentCount; index++)
    {
        if (index > 1)
        {
            printf(" ");
        }

        JsValueRef stringValue = JS_INVALID_REFERENCE;
        IfFailThrowJsError(JsConvertValueToString(arguments[index], &stringValue), "invalid argument");

        size_t length = 0;
        IfFailThrowJsError(JsCopyString(stringValue, nullptr, 0, &length), "invalid argument");
        std::vector<char> stringData(length);
        IfFailThrowJsError(JsCopyString(stringValue, stringData.data(), static_cast<int>(stringData.size()), nullptr), "invalid argument");
        std::string message(begin(stringData), end(stringData));

        wprintf(GetWideString(message.c_str()).c_str());
    }

    printf("\n");

    return JS_INVALID_REFERENCE;
}

//
// Callback to test handling of exceptions thrown from native code. This isn't a realistic function for a host to
// implement, simply a sample function for testing purposes only.
//
JsValueRef CALLBACK HostThrow(
    JsValueRef /*callee*/,
    bool /*isConstructCall*/,
    JsValueRef* arguments,
    unsigned short argumentCount,
    void* /*callbackState*/)
{
    JsValueRef error = JS_INVALID_REFERENCE;

    if (argumentCount >= 2)
    {
        // Attempt to use the provided object as the error to set.
        error = arguments[1];
    }
    else
    {
        // By default create a sample error object with a message.
        const std::string errStr("Sample error message");
        JsValueRef errorMsg = JS_INVALID_REFERENCE;
        JsCreateString(errStr.c_str(), errStr.length(), &errorMsg);

        JsCreateError(errorMsg, &error);
    }

    JsSetException(error);

    return JS_INVALID_REFERENCE;
}

//
// Callback to load a script and run it.
//
JsValueRef CALLBACK HostRunScript(
    JsValueRef /*callee*/, 
    bool /*isConstructCall*/, 
    JsValueRef* arguments, 
    unsigned short argumentCount, 
    void* /*callbackState*/)
{
    JsValueRef result = JS_INVALID_REFERENCE;

    if (argumentCount < 2)
    {
        ThrowJsError("not enough arguments");
        return result;
    }

    // Convert filename.
    size_t length = 0;
    IfFailThrowJsError(JsCopyString(arguments[1], nullptr, 0, &length), "invalid filename argument");

    std::vector<char> data(length);
    IfFailThrowJsError(JsCopyString(arguments[1], data.data(), data.size(), &length), "invalid filename argument");
    IfFailThrowJsError(RunScript(data.data(), &result), "failed to run script");

    return result;
}

//
// Helper to define a host callback method on the global host object.
//
JsErrorCode DefineHostCallback(
    JsValueRef globalObject,
    const char* callbackName,
    JsNativeFunction callback,
    void* callbackState)
{
    // Get property ID.
    JsPropertyIdRef propertyId = JS_INVALID_REFERENCE;
    IfFailRet(JsCreatePropertyId(callbackName, strlen(callbackName), &propertyId));

    // Create a function
    JsValueRef function = JS_INVALID_REFERENCE;
    IfFailRet(JsCreateFunction(callback, callbackState, &function));

    // Set the property
    IfFailRet(JsSetProperty(globalObject, propertyId, function, true));

    return JsNoError;
}

//
// Creates a host execution context and sets up the host object in it.
//
JsErrorCode CreateHostContext(JsRuntimeHandle runtime, std::vector<std::string>& scriptArgs, JsContextRef* context)
{
    // Create the context.
    IfFailRet(JsCreateContext(runtime, context));

    // Now set the execution context as being the current one on this thread.
    IfFailRet(JsSetCurrentContext(*context));

    // Create the host object the script will use.
    JsValueRef hostObject = JS_INVALID_REFERENCE;
    IfFailRet(JsCreateObject(&hostObject));

    // Get the global object
    JsValueRef globalObject = JS_INVALID_REFERENCE;
    IfFailRet(JsGetGlobalObject(&globalObject));

    // Get the name of the property ("host") that we're going to set on the global object.
    JsPropertyIdRef hostPropertyId = JS_INVALID_REFERENCE;
    static const char* hostIdName = "host";
    IfFailRet(JsCreatePropertyId(hostIdName, strlen(hostIdName), &hostPropertyId));

    // Set the property.
    IfFailRet(JsSetProperty(globalObject, hostPropertyId, hostObject, true));

    // Now create the host callbacks that we're going to expose to the script.
    IfFailRet(DefineHostCallback(hostObject, "echo", HostEcho, nullptr));
    IfFailRet(DefineHostCallback(hostObject, "runScript", HostRunScript, nullptr));
    IfFailRet(DefineHostCallback(hostObject, "throw", HostThrow, nullptr));

    // Create an array for arguments.
    JsValueRef arguments = JS_INVALID_REFERENCE;
    unsigned int argCount = static_cast<unsigned int>(scriptArgs.size());
    IfFailRet(JsCreateArray(argCount, &arguments));

    for (unsigned int index = 0; index < argCount; index++)
    {
        // Create the argument value.
        std::string& str = scriptArgs[index];

        JsValueRef argument = JS_INVALID_REFERENCE;
        IfFailRet(JsCreateString(str.c_str(), str.length(), &argument));

        // Create the index.
        JsValueRef indexValue = JS_INVALID_REFERENCE;
        IfFailRet(JsIntToNumber(index, &indexValue));

        // Set the value.
        IfFailRet(JsSetIndexedProperty(arguments, indexValue, argument));
    }

    // Get the name of the property that we're going to set on the host object.
    JsPropertyIdRef argumentsPropertyId = JS_INVALID_REFERENCE;
    static const char* argumentsPropertyIdName = "arguments";
    IfFailRet(JsCreatePropertyId(argumentsPropertyIdName, strlen(argumentsPropertyIdName), &argumentsPropertyId));

    // Set the arguments property.
    IfFailRet(JsSetProperty(hostObject, argumentsPropertyId, arguments, true));

    // Clean up the current execution context.
    IfFailRet(JsSetCurrentContext(JS_INVALID_REFERENCE));

    return JsNoError;
}

//
// Print out a script exception.
//
JsErrorCode PrintScriptException()
{
    // Get script exception.
    JsValueRef exception = JS_INVALID_REFERENCE;
    IfFailRet(JsGetAndClearException(&exception));

    // Get message.
    JsPropertyIdRef messagePropertyId = JS_INVALID_REFERENCE;
    static const char* messagePropertyIdName = "message";
    IfFailRet(JsCreatePropertyId(messagePropertyIdName, strlen(messagePropertyIdName), &messagePropertyId));

    JsValueRef messageValue = JS_INVALID_REFERENCE;
    IfFailRet(JsGetProperty(exception, messagePropertyId, &messageValue));

    size_t length = 0;
    IfFailRet(JsCopyString(messageValue, nullptr, 0, &length));
    std::vector<char> messageData(length);
    IfFailRet(JsCopyString(messageValue, messageData.data(), static_cast<int>(messageData.size()), nullptr));
    std::string message(begin(messageData), end(messageData));

    fwprintf(stderr, L"chakrahost: exception: %s\n", GetWideString(message.c_str()).c_str());

    return JsNoError;
}

JsErrorCode EnableDebugging(
    JsRuntimeHandle runtime,
    std::string const& runtimeName,
    bool breakOnNextLine, 
    uint16_t port, 
    std::unique_ptr<DebugProtocolHandler>& debugProtocolHandler,
    std::unique_ptr<DebugService>& debugService)
{
    JsErrorCode result = JsNoError;
    auto protocolHandler = std::make_unique<DebugProtocolHandler>(runtime);
    auto service = std::make_unique<DebugService>();

    result = service->RegisterHandler(runtimeName, *protocolHandler, breakOnNextLine);

    if (result == JsNoError)
    {
        result = service->Listen(port);

        std::cout << "Listening on ws://127.0.0.1:" << port << "/" << runtimeName << std::endl;
    }

    if (result == JsNoError)
    {
        debugProtocolHandler = std::move(protocolHandler);
        debugService = std::move(service);
    }

    return result;
}

//
// The main entry point for the host.
//
int _cdecl wmain(int argc, wchar_t* argv[])
{
    int returnValue = EXIT_FAILURE;
    CommandLineArguments arguments;

    arguments.ParseCommandLine(argc, argv);

    if (arguments.help)
    {
        arguments.ShowHelp();
        return returnValue;
    }

    try
    {
        JsRuntimeHandle runtime = JS_INVALID_RUNTIME_HANDLE;
        JsContextRef context = JS_INVALID_REFERENCE;
        std::unique_ptr<DebugProtocolHandler> debugProtocolHandler;
        std::unique_ptr<DebugService> debugService;
        std::string runtimeName("runtime1");

        // Create the runtime. We're only going to use one runtime for this host.
        IfFailError(
            JsCreateRuntime(JsRuntimeAttributeDispatchSetExceptionsToDebugger, nullptr, &runtime),
            "failed to create runtime.");

        if (arguments.enableDebugging)
        {
            IfFailError(
                EnableDebugging(runtime, runtimeName, arguments.breakOnNextLine, static_cast<uint16_t>(arguments.port), debugProtocolHandler, debugService),
                "failed to enable debugging.");
        }

        // Similarly, create a single execution context. Note that we're putting it on the stack here,
        // so it will stay alive through the entire run.
        IfFailError(CreateHostContext(runtime, arguments.scriptArgs, &context), "failed to create execution context.");

        // Now set the execution context as being the current one on this thread.
        IfFailError(JsSetCurrentContext(context), "failed to set current context.");

        if (debugProtocolHandler && arguments.breakOnNextLine)
        {
            std::cout << "Waiting for debugger to connect..." << std::endl;
            IfFailError(debugProtocolHandler->WaitForDebugger(), "failed to wait for debugger");
            std::cout << "Debugger connected" << std::endl;
        }

        // Run the script.
        JsValueRef result = JS_INVALID_REFERENCE;
        JsErrorCode errorCode = RunScript(arguments.scriptArgs[0].c_str(), &result);

        if (errorCode == JsErrorScriptException)
        {
            IfFailError(PrintScriptException(), "failed to print exception");
            return EXIT_FAILURE;
        }
        else
        {
            IfFailError(errorCode, "failed to run script.");
        }

        // Convert the return value.
        JsValueRef numberResult = JS_INVALID_REFERENCE;
        double doubleResult;
        IfFailError(JsConvertValueToNumber(result, &numberResult), "failed to convert return value.");
        IfFailError(JsNumberToDouble(numberResult, &doubleResult), "failed to convert return value.");
        returnValue = (int)doubleResult;
        std::cout << returnValue << std::endl;

        // Clean up the current execution context.
        IfFailError(JsSetCurrentContext(JS_INVALID_REFERENCE), "failed to cleanup current context.");
        context = JS_INVALID_REFERENCE;

        if (debugService)
        {
            IfFailError(debugService->Close(), "failed to close service");
            IfFailError(debugService->UnregisterHandler(runtimeName), "failed to unregister handler");
            IfFailError(debugService->Destroy(), "failed to destroy service");
        }

        if (debugProtocolHandler)
        {
            IfFailError(debugProtocolHandler->Destroy(), "failed to destroy handler");
        }

        // Clean up the runtime.
        IfFailError(JsDisposeRuntime(runtime), "failed to cleanup runtime.");
    }
    catch (...)
    {
        fprintf(stderr, "chakrahost: fatal error: internal error.\n");
    }

error:
    return returnValue;
}
