#if defined(DM_PLATFORM_ANDROID)

#include <jni.h>
#include <stdlib.h>
#include <unistd.h>

#include <dmsdk/dlib/array.h>
#include <dmsdk/dlib/log.h>
#include <dmsdk/dlib/mutex.h>
#include <dmsdk/script/script.h>
#include <dmsdk/extension/extension.h>
#include <android_native_app_glue.h>

#include "webview_common.h"

extern struct android_app* g_AndroidApp;

enum CommandType
{
    CMD_LOAD_OK,
    CMD_LOAD_ERROR,
    CMD_EVAL_OK,
    CMD_EVAL_ERROR,
    CMD_LOADING,
};

struct WebViewCommand
{
    WebViewCommand()
    {
        memset(this, 0, sizeof(WebViewCommand));
    }
    CommandType m_Type;
    int         m_WebViewID;
    int         m_RequestID;
    void*       m_Data;
    const char* m_Url;
};

static JNIEnv* Attach()
{
    JNIEnv* env = 0;
    g_AndroidApp->activity->vm->AttachCurrentThread(&env, NULL);
    return env;
}

static void Detach()
{
    g_AndroidApp->activity->vm->DetachCurrentThread();
}

struct WebViewExtensionState
{
    WebViewExtensionState()
    {
        memset(this, 0, sizeof(*this));
    }

    void Clear()
    {
        for( int i = 0; i < dmWebView::MAX_NUM_WEBVIEWS; ++i )
        {
            ClearWebViewInfo(&m_Info[i]);
        }
        memset(m_RequestIds, 0, sizeof(m_RequestIds));
    }

    dmWebView::WebViewInfo  m_Info[dmWebView::MAX_NUM_WEBVIEWS];
    bool                    m_Used[dmWebView::MAX_NUM_WEBVIEWS];
    int                     m_RequestIds[dmWebView::MAX_NUM_WEBVIEWS];
    jobject                 m_WebViewJNI;
    jmethodID               m_Create;
    jmethodID               m_Destroy;
    jmethodID               m_Load;
    jmethodID               m_LoadRaw;
    jmethodID               m_ContinueLoading;
    jmethodID               m_Eval;
    jmethodID               m_SetVisible;
    jmethodID               m_IsVisible;
    jmethodID               m_SetPosition;
    dmMutex::HMutex         m_Mutex;
    dmArray<WebViewCommand> m_CmdQueue;
};

WebViewExtensionState g_WebView;

namespace dmWebView
{

#define CHECK_WEBVIEW_AND_RETURN() if( webview_id >= MAX_NUM_WEBVIEWS || webview_id < 0 ) { dmLogError("%s: Invalid webview_id: %d", __FUNCTION__, webview_id); return -1; }

int Platform_Create(lua_State* L, dmWebView::WebViewInfo* _info)
{
    // Find a free slot
    int webview_id = -1;
    for( int i = 0; i < MAX_NUM_WEBVIEWS; ++i )
    {
        if( !g_WebView.m_Used[i] )
        {
            webview_id = i;
            break;
        }
    }

    if( webview_id == -1 )
    {
        dmLogError("Max number of webviews already opened: %d", MAX_NUM_WEBVIEWS);
        return -1;
    }

    g_WebView.m_Used[webview_id] = true;
    g_WebView.m_Info[webview_id] = *_info;

    JNIEnv* env = Attach();
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_Create, webview_id);
    Detach();

    return webview_id;
}

static void DestroyWebView(int webview_id)
{
    ClearWebViewInfo(&g_WebView.m_Info[webview_id]);
    g_WebView.m_Used[webview_id] = false;
}

int Platform_Destroy(lua_State* L, int webview_id)
{
    CHECK_WEBVIEW_AND_RETURN();
    JNIEnv* env = Attach();
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_Destroy, webview_id);
    Detach();
    DestroyWebView(webview_id);
    return 0;
}

int Platform_Open(lua_State* L, int webview_id, const char* url, dmWebView::RequestInfo* options)
{
    CHECK_WEBVIEW_AND_RETURN();
    int request_id = ++g_WebView.m_RequestIds[webview_id];

    JNIEnv* env = Attach();
    jstring jurl = env->NewStringUTF(url);
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_Load, jurl, webview_id, request_id, options->m_Hidden);
    env->DeleteLocalRef(jurl);
    Detach();
    return request_id;
}

int Platform_ContinueOpen(lua_State* L, int webview_id, int request_id, const char* url)
{
    CHECK_WEBVIEW_AND_RETURN();

    JNIEnv* env = Attach();
    jstring jurl = env->NewStringUTF(url);
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_ContinueLoading, jurl, webview_id, request_id);
    env->DeleteLocalRef(jurl);
    Detach();
    return request_id;
}

int Platform_OpenRaw(lua_State* L, int webview_id, const char* html, dmWebView::RequestInfo* options)
{
    CHECK_WEBVIEW_AND_RETURN();
    int request_id = ++g_WebView.m_RequestIds[webview_id];

    JNIEnv* env = Attach();
    jstring jhtml = env->NewStringUTF(html);
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_LoadRaw, jhtml, webview_id, request_id, options->m_Hidden);
    env->DeleteLocalRef(jhtml);
    Detach();
    return request_id;
}

int Platform_Eval(lua_State* L, int webview_id, const char* code)
{
    CHECK_WEBVIEW_AND_RETURN();
    int request_id = ++g_WebView.m_RequestIds[webview_id];

    JNIEnv* env = Attach();
    jstring jcode = env->NewStringUTF(code);
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_Eval, jcode, webview_id, request_id);
    Detach();
    return request_id;
}

int Platform_SetVisible(lua_State* L, int webview_id, int visible)
{
    CHECK_WEBVIEW_AND_RETURN();
    JNIEnv* env = Attach();
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_SetVisible, webview_id, visible);
    Detach();
    return 0;
}

int Platform_IsVisible(lua_State* L, int webview_id)
{
    CHECK_WEBVIEW_AND_RETURN();
    JNIEnv* env = Attach();
    int visible = env->CallIntMethod(g_WebView.m_WebViewJNI, g_WebView.m_IsVisible, webview_id);
    Detach();
    return visible;
}

int Platform_SetPosition(lua_State* L, int webview_id, int x, int y, int width, int height)
{
    CHECK_WEBVIEW_AND_RETURN();
    JNIEnv* env = Attach();
    env->CallVoidMethod(g_WebView.m_WebViewJNI, g_WebView.m_SetPosition, webview_id, x, y, width, height);
    Detach();
    return 0;
}

#undef CHECK_WEBVIEW_AND_RETURN

static char* CopyString(JNIEnv* env, jstring s)
{
    const char* javastring = env->GetStringUTFChars(s, 0);
    char* copy = strdup(javastring);
    env->ReleaseStringUTFChars(s, javastring);
    return copy;
}

static void QueueCommand(WebViewCommand* cmd)
{
    DM_MUTEX_SCOPED_LOCK(g_WebView.m_Mutex);
    if (g_WebView.m_CmdQueue.Full())
    {
        g_WebView.m_CmdQueue.OffsetCapacity(8);
    }
    g_WebView.m_CmdQueue.Push(*cmd);
}


#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL Java_com_defold_webview_WebViewJNI_onPageFinished(JNIEnv* env, jobject, jstring url, jint webview_id, jint request_id)
{
    WebViewCommand cmd;
    cmd.m_Type = CMD_LOAD_OK;
    cmd.m_WebViewID = webview_id;
    cmd.m_RequestID = request_id;
    cmd.m_Url = CopyString(env, url);
    QueueCommand(&cmd);
}

JNIEXPORT void JNICALL Java_com_defold_webview_WebViewJNI_onReceivedError(JNIEnv* env, jobject, jstring url, jint webview_id, jint request_id, jstring errorMessage)
{
    WebViewCommand cmd;
    cmd.m_Type = CMD_LOAD_ERROR;
    cmd.m_WebViewID = webview_id;
    cmd.m_RequestID = request_id;
    cmd.m_Url = CopyString(env, url);
    cmd.m_Data = CopyString(env, errorMessage);
    QueueCommand(&cmd);
}

JNIEXPORT void JNICALL Java_com_defold_webview_WebViewJNI_onEvalFinished(JNIEnv* env, jobject, jstring result, jint webview_id, jint request_id)
{
    WebViewCommand cmd;
    cmd.m_Type = CMD_EVAL_OK;
    cmd.m_WebViewID = webview_id;
    cmd.m_RequestID = request_id;
    cmd.m_Url = 0;
    cmd.m_Data = CopyString(env, result);
    QueueCommand(&cmd);
}

JNIEXPORT void JNICALL Java_com_defold_webview_WebViewJNI_onEvalFailed(JNIEnv* env, jobject, jstring error, jint webview_id, jint request_id)
{
    WebViewCommand cmd;
    cmd.m_Type = CMD_EVAL_ERROR;
    cmd.m_WebViewID = webview_id;
    cmd.m_RequestID = request_id;
    cmd.m_Url = 0;
    cmd.m_Data = CopyString(env, error);
    QueueCommand(&cmd);
}

JNIEXPORT void JNICALL Java_com_defold_webview_WebViewJNI_onPageLoading(JNIEnv* env, jobject, jstring url, jint webview_id, jint request_id)
{
    WebViewCommand cmd;
    cmd.m_Type = CMD_LOADING;
    cmd.m_WebViewID = webview_id;
    cmd.m_RequestID = request_id;
    cmd.m_Url = CopyString(env, url);
    QueueCommand(&cmd);
}

#ifdef __cplusplus
}
#endif

dmExtension::Result Platform_Update(dmExtension::Params* params)
{
    if (g_WebView.m_CmdQueue.Empty())
        return dmExtension::RESULT_OK; // avoid a lock (~300us on iPhone 4s)

    DM_MUTEX_SCOPED_LOCK(g_WebView.m_Mutex);
    for (uint32_t i=0; i != g_WebView.m_CmdQueue.Size(); ++i)
    {
        const WebViewCommand& cmd = g_WebView.m_CmdQueue[i];

        dmWebView::CallbackInfo cbinfo;
        switch (cmd.m_Type)
        {
        case CMD_LOADING:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = cmd.m_Url;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_URL_LOADING;
            cbinfo.m_Result = 0;
            RunCallback(&cbinfo);
            break;

        case CMD_LOAD_OK:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = cmd.m_Url;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_URL_OK;
            cbinfo.m_Result = 0;
            RunCallback(&cbinfo);
            break;

        case CMD_LOAD_ERROR:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = cmd.m_Url;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_URL_ERROR;
            cbinfo.m_Result = (const char*)cmd.m_Data;
            RunCallback(&cbinfo);
            break;

        case CMD_EVAL_OK:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = 0;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_EVAL_OK;
            cbinfo.m_Result = (const char*)cmd.m_Data;
            RunCallback(&cbinfo);
            break;

        case CMD_EVAL_ERROR:
            cbinfo.m_Info = &g_WebView.m_Info[cmd.m_WebViewID];
            cbinfo.m_WebViewID = cmd.m_WebViewID;
            cbinfo.m_RequestID = cmd.m_RequestID;
            cbinfo.m_Url = 0;
            cbinfo.m_Type = dmWebView::CALLBACK_RESULT_EVAL_ERROR;
            cbinfo.m_Result = (const char*)cmd.m_Data;
            RunCallback(&cbinfo);
            break;

        default:
            assert(false);
        }
        if (cmd.m_Url) {
            free((void*)cmd.m_Url);
        }
        if (cmd.m_Data) {
            free(cmd.m_Data);
        }
    }
    g_WebView.m_CmdQueue.SetSize(0);
    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_AppInitialize(dmExtension::AppParams* params)
{
    g_WebView.m_Mutex = dmMutex::New();
    g_WebView.m_CmdQueue.SetCapacity(8);

    JNIEnv* env = Attach();

    jclass activity_class = env->FindClass("android/app/NativeActivity");
    jmethodID get_class_loader = env->GetMethodID(activity_class,"getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject cls = env->CallObjectMethod(g_AndroidApp->activity->clazz, get_class_loader);
    jclass class_loader = env->FindClass("java/lang/ClassLoader");
    jmethodID find_class = env->GetMethodID(class_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring str_class_name = env->NewStringUTF("com.defold.webview.WebViewJNI");
    jclass webview_class = (jclass)env->CallObjectMethod(cls, find_class, str_class_name);
    env->DeleteLocalRef(str_class_name);

    g_WebView.m_Create = env->GetMethodID(webview_class, "create", "(I)V");
    g_WebView.m_Destroy = env->GetMethodID(webview_class, "destroy", "(I)V");
    g_WebView.m_Load = env->GetMethodID(webview_class, "load", "(Ljava/lang/String;III)V");
    g_WebView.m_LoadRaw = env->GetMethodID(webview_class, "loadRaw", "(Ljava/lang/String;III)V");
    g_WebView.m_ContinueLoading = env->GetMethodID(webview_class, "continueLoading", "(Ljava/lang/String;II)V");
    g_WebView.m_Eval = env->GetMethodID(webview_class, "eval", "(Ljava/lang/String;II)V");
    g_WebView.m_SetVisible = env->GetMethodID(webview_class, "setVisible", "(II)V");
    g_WebView.m_IsVisible = env->GetMethodID(webview_class, "isVisible", "(I)I");
    g_WebView.m_SetPosition = env->GetMethodID(webview_class, "setPosition", "(IIIII)V");

    jmethodID jni_constructor = env->GetMethodID(webview_class, "<init>", "(Landroid/app/Activity;I)V");
    g_WebView.m_WebViewJNI = env->NewGlobalRef(env->NewObject(webview_class, jni_constructor, g_AndroidApp->activity->clazz, dmWebView::MAX_NUM_WEBVIEWS));

    Detach();

    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_Initialize(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_AppFinalize(dmExtension::AppParams* params)
{
    JNIEnv* env = Attach();
    env->DeleteGlobalRef(g_WebView.m_WebViewJNI);
    Detach();
    g_WebView.m_WebViewJNI = NULL;

    dmMutex::Delete(g_WebView.m_Mutex);

    return dmExtension::RESULT_OK;
}

dmExtension::Result Platform_Finalize(dmExtension::Params* params)
{
    dmLogError("Platform Finalize Called");
    for( int i = 0; i < dmWebView::MAX_NUM_WEBVIEWS; ++i )
    {
        if (g_WebView.m_Used[i]) {
            DestroyWebView(i);
        }
    }

    DM_MUTEX_SCOPED_LOCK(g_WebView.m_Mutex);
    for (uint32_t i=0; i != g_WebView.m_CmdQueue.Size(); ++i)
    {
        const WebViewCommand& cmd = g_WebView.m_CmdQueue[i];
        if (cmd.m_Url) {
            free((void*)cmd.m_Url);
        }
    }
    g_WebView.m_CmdQueue.SetSize(0);
    return dmExtension::RESULT_OK;
}

} // namespace dmWebView

#endif // DM_PLATFORM_ANDROID
