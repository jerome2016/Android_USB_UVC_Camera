package jerome.com.usbcamera;

import android.graphics.Bitmap;

public class ImageProc {

    public static final int CAMERA_PIX_FMT_MJPEG = 0; // V4L2_PIX_FMT_MJPEG
    public static final int CAMERA_PIX_FMT_YUYV = 1; // V4L2_PIX_FMT_YUYV

    public static final int IMG_WIDTH = 1280;
    public static final int IMG_HEIGHT = 720;

    private static RecordCallback mRecordCallback;

    public ImageProc() {
    }

    public interface RecordCallback {
        void onDataEncode(byte[] data);
    }

    public void setRecordCallback(RecordCallback callback){
        mRecordCallback = callback;
    }

    public static void encodeYuv2H264(byte[] yuv420sp){
        mRecordCallback.onDataEncode(yuv420sp);
    }

    /*
    * width, height: (640, 480), (1280, 720)
    */
    public native int nativePrepareCamera(int width, int height, int pixelFormat);
    public native int nativeProcessCamera();
    public native int nativeStopCamera();
    public native void nativePixelToBmp(Bitmap bitmap);
    public native int nativeStartRecord();
    public native int nativeStopRecord();

    static {
        System.loadLibrary("UsbCameraProc");
    }
}
