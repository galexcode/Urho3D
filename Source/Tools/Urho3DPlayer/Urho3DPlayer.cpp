//
// Copyright (c) 2008-2013 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Engine.h"
#include "FileSystem.h"
#include "Log.h"
#include "Main.h"
#include "ProcessUtils.h"
#include "ResourceCache.h"
#include "ResourceEvents.h"
#include "Script.h"
#include "ScriptFile.h"
#include "Urho3DPlayer.h"

#ifdef ENABLE_LUA
#include "LuaScript.h"
#endif

#include "DebugNew.h"

DEFINE_APPLICATION_MAIN(Urho3DPlayer);

Urho3DPlayer::Urho3DPlayer(Context* context) :
    Application(context)
{
}

void Urho3DPlayer::Setup()
{
    // On Android and iOS, read command line from a file as parameters can not otherwise be easily given
    #if defined(ANDROID) || defined(IOS)
    SharedPtr<File> commandFile(new File(context_, GetSubsystem<FileSystem>()->GetProgramDir() + "Data/CommandLine.txt",
        FILE_READ));
    String commandLine = commandFile->ReadLine();
    ParseArguments(commandLine, false);
    commandFile->Close();
    #endif

    // Check for script file name
    const Vector<String>& arguments = GetArguments();
    String scriptFileName;
    for (unsigned i = 0; i < arguments.Size(); ++i)
    {
        if (arguments[i][0] != '-')
        {
            scriptFileName_ = GetInternalPath(arguments[i]);
            break;
        }
    }

    // Show usage if not found
    if (scriptFileName_.Empty())
    {
        ErrorExit("Usage: Urho3DPlayer <scriptfile> [options]\n\n"
            "The script file should implement the function void Start() for initializing the "
            "application and subscribing to all necessary events, such as the frame update.\n"
            #ifndef WIN32
            "\nCommand line options:\n"
            "-x <res>    Horizontal resolution\n"
            "-y <res>    Vertical resolution\n"
            "-m <level>  Enable hardware multisampling\n"
            "-v          Enable vertical sync\n"
            "-t          Enable triple buffering\n"
            "-w          Start in windowed mode\n"
            "-s          Enable resizing when in windowed mode\n"
            "-q          Enable quiet mode which does not log to standard output stream\n"
            "-b <length> Sound buffer length in milliseconds\n"
            "-r <freq>   Sound mixing frequency in Hz\n"
            "-p <paths>  Resource path(s) to use, separated by semicolons\n"
            "-log<level> Change the log level, valid 'level' values are 'debug', 'info', 'warning', 'error'\n"
            "-headless   Headless mode. No application window will be created\n"
            "-prepass    Use light pre-pass rendering\n"
            "-deferred   Use deferred rendering\n"
            "-lqshadows  Use low-quality (1-sample) shadow filtering\n"
            "-noshadows  Disable shadow rendering\n"
            "-nolimit    Disable frame limiter\n"
            "-nothreads  Disable worker threads\n"
            "-nosound    Disable sound output\n"
            "-noip       Disable sound mixing interpolation\n"
            "-sm2        Force SM2.0 rendering\n"
            #endif
        );
    }
}

void Urho3DPlayer::Start()
{
#ifdef ENABLE_LUA
    String extension = GetExtension(scriptFileName_);
    if (extension != ".lua")
    {
#endif
        // Instantiate and register the AngelScript subsystem
        context_->RegisterSubsystem(new Script(context_));

        // Hold a shared pointer to the script file to make sure it is not unloaded during runtime
        scriptFile_ = GetSubsystem<ResourceCache>()->GetResource<ScriptFile>(scriptFileName_);

        // If script loading is successful, proceed to main loop
        if (scriptFile_ && scriptFile_->Execute("void Start()"))
        {
            // Subscribe to script's reload event to allow live-reload of the application
            SubscribeToEvent(scriptFile_, E_RELOADSTARTED, HANDLER(Urho3DPlayer, HandleScriptReloadStarted));
            SubscribeToEvent(scriptFile_, E_RELOADFINISHED, HANDLER(Urho3DPlayer, HandleScriptReloadFinished));
            SubscribeToEvent(scriptFile_, E_RELOADFAILED, HANDLER(Urho3DPlayer, HandleScriptReloadFailed));
            return;
        }
#ifdef ENABLE_LUA
    }
    else
    {
        // Instantiate and register the Lua script subsystem
        LuaScript* luaScript = new LuaScript(context_);
        context_->RegisterSubsystem(luaScript);

        // If script loading is successful, proceed to main loop
        if (luaScript->ExecuteFile(scriptFileName_))
        {
            luaScript->ExecuteFunction("Start");
            return;
        }
    }
#endif

    // The script was not successfully loaded. Show the last error message and do not run the main loop
    ErrorExit();
}

void Urho3DPlayer::Stop()
{
    if (scriptFile_)
    {
        // Execute the optional stop function
        if (scriptFile_->GetFunction("void Stop()"))
            scriptFile_->Execute("void Stop()");
    }
#ifdef ENABLE_LUA
    else
    {
        LuaScript* luaScript = GetSubsystem<LuaScript>();
        if (luaScript && luaScript->GetFunction("Stop", true))
            luaScript->ExecuteFunction("Stop");
    }
#endif
}

void Urho3DPlayer::HandleScriptReloadStarted(StringHash eventType, VariantMap& eventData)
{
    if (scriptFile_->GetFunction("void Stop()"))
        scriptFile_->Execute("void Stop()");
}

void Urho3DPlayer::HandleScriptReloadFinished(StringHash eventType, VariantMap& eventData)
{
    // Restart the script application after reload
    if (!scriptFile_->Execute("void Start()"))
    {
        scriptFile_.Reset();
        ErrorExit();
    }
}

void Urho3DPlayer::HandleScriptReloadFailed(StringHash eventType, VariantMap& eventData)
{
    scriptFile_.Reset();
    ErrorExit();
}
