package org.libsdl.android;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.egl.*;

import android.app.*;
import android.content.*;
import android.view.*;
import android.os.*;
import android.util.Log;
import android.graphics.*;
import android.text.method.*;
import android.text.*;

import java.lang.*;


/**
    SDL Activity
*/
public class SDLActivity extends Activity {

    //Main components
    private static SDLActivity mSingleton;
    private static SDLSurface mSurface;

    //Load the .so
    static {
        System.loadLibrary("sdltest");
    }

    //Setup
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        //So we can call stuff from static callbacks
        mSingleton = this;

        //Set up the surface
        mSurface = new SDLSurface(getApplication());
        setContentView(mSurface);
        SurfaceHolder holder = mSurface.getHolder();
        holder.setType(SurfaceHolder.SURFACE_TYPE_GPU); 

        
    }

    //Events
    protected void onPause() {
        super.onPause();
    }

    protected void onResume() {
        super.onResume();
    }

    



    //C functions we call
    public static native void nativeInit();
    public static native void onNativeKeyDown(int keycode);
    public static native void onNativeKeyUp(int keycode);






    //Java functions called from C
    private static void createGLContext(){
        mSurface.initEGL();
    }

    public static void flipBuffers(){
        mSurface.flipEGL();
    }






    
    
}

/**
    Simple nativeInit() runnable
*/
class SDLRunner implements Runnable{
    public void run(){
        //Runs SDL_main()
        SDLActivity.nativeInit();
    }
}


/**
    SDLSurface. This is what we draw on, so we need to know when it's created
    in order to do anything useful. 

    Because of this, that's where we set up the SDL thread
*/
class SDLSurface extends SurfaceView implements SurfaceHolder.Callback, View.OnKeyListener  {

    //This is what SDL runs in. It invokes SDL_main(), eventually
    private Thread mSDLThread;    
    
    //EGL private objects
    private EGLContext  mEGLContext;
    private EGLSurface  mEGLSurface;
    private EGLDisplay  mEGLDisplay;

    //Startup    
    public SDLSurface(Context context) {
        super(context);
        getHolder().addCallback(this); 
    
        setFocusable(true);
        setFocusableInTouchMode(true);
        requestFocus();
        setOnKeyListener(this);      
    }

    //Called when we have a valid drawing surface
    public void surfaceCreated(SurfaceHolder holder) {
        Log.v("SDL","Surface created"); 

        mSDLThread = new Thread(new SDLRunner(), "SDLThread"); 
		mSDLThread.start();       
    }

    //Called when we lose the surface
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.v("SDL","Surface destroyed");
    }

    //Called when the surface is resized
    public void surfaceChanged(SurfaceHolder holder, int format, 
                                int width, int height) {
        Log.v("SDL","Surface resized");
    }

    //unused
    public void onDraw(Canvas canvas) {}

    
    //EGL functions
    public boolean initEGL(){
        Log.v("SDL","Starting up");

        try{

            EGL10 egl = (EGL10)EGLContext.getEGL();

            EGLDisplay dpy = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);

            int[] version = new int[2];
            egl.eglInitialize(dpy, version);

            int[] configSpec = {
                    //EGL10.EGL_DEPTH_SIZE,   16,
                    EGL10.EGL_NONE
            };
            EGLConfig[] configs = new EGLConfig[1];
            int[] num_config = new int[1];
            egl.eglChooseConfig(dpy, configSpec, configs, 1, num_config);
            EGLConfig config = configs[0];

            EGLContext ctx = egl.eglCreateContext(dpy, config, EGL10.EGL_NO_CONTEXT, null);

            EGLSurface surface = egl.eglCreateWindowSurface(dpy, config, this, null);

            egl.eglMakeCurrent(dpy, surface, surface, ctx);

            mEGLContext = ctx;
            mEGLDisplay = dpy;
            mEGLSurface = surface;
            
            
        }catch(Exception e){
            Log.v("SDL", e + "");
            for(StackTraceElement s : e.getStackTrace()){
                Log.v("SDL", s.toString());
            }
        }
        Log.v("SDL","Done making!");

        return true;
    }

    //EGL buffer flip
    public void flipEGL(){      
        try{
        
            EGL10 egl = (EGL10)EGLContext.getEGL();
            GL10 gl = (GL10)mEGLContext.getGL();

            egl.eglWaitNative(EGL10.EGL_NATIVE_RENDERABLE, null);

            //drawing here

            egl.eglWaitGL();

            egl.eglSwapBuffers(mEGLDisplay, mEGLSurface);

            
        }catch(Exception e){
            Log.v("SDL", "flipEGL(): " + e);

            for(StackTraceElement s : e.getStackTrace()){
                Log.v("SDL", s.toString());
            }
        }
    }


  

    public boolean onKey(View  v, int keyCode, KeyEvent event){

        if(event.getAction() == KeyEvent.ACTION_DOWN){
            SDLActivity.onNativeKeyDown(keyCode);
            return true;
        }
        
        else if(event.getAction() == KeyEvent.ACTION_UP){
            SDLActivity.onNativeKeyUp(keyCode);
            return true;
        }
        
        return false;
    }


}


