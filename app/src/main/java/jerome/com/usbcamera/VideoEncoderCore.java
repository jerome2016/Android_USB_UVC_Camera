
package jerome.com.usbcamera;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;

public class VideoEncoderCore {
    private static final String TAG = "VideoEncoderCore";
    private static final boolean VERBOSE = true;

    // TODO: these ought to be configurable as well
    private static final String MIME_TYPE = "video/avc";    // H.264 Advanced Video Coding
    private static final int FRAME_RATE = 15;               // fps
    private static final int IFRAME_INTERVAL = 5;           // 5 seconds between I-frames
    private static final int TIMEOUT_USEC = 10000;
    private static final int COMPRESS_RATIO = 16;   //录像质量压缩比

    //private Surface mInputSurface;
    private MediaMuxerCore mMuxerCore;
    private MediaCodec mediaCodec;
    private MediaCodec.BufferInfo mBufferInfo;
    private boolean mMuxerStarted;
    private boolean mIsEncodeing = false;

    /**
     * Configures encoder and muxer state, and prepares the input Surface.
     */
    public VideoEncoderCore(MediaMuxerCore muxer, int width, int height)
            throws IOException {

        int bitRate = width * height * 8 * FRAME_RATE / COMPRESS_RATIO;
        mMuxerCore = muxer;
        mBufferInfo = new MediaCodec.BufferInfo();

        MediaFormat format = MediaFormat.createVideoFormat(MIME_TYPE, width, height);

        format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Planar);
        format.setInteger(MediaFormat.KEY_BIT_RATE, bitRate);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, IFRAME_INTERVAL);
        if (VERBOSE) Log.d(TAG, "format: " + format);

        mediaCodec = MediaCodec.createEncoderByType(MIME_TYPE);
        mediaCodec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        mediaCodec.start();

        mMuxerStarted = false;
    }

    /**
     * Releases encoder resources.
     */
    public void release() {

        while(mIsEncodeing){
            try{
                Thread.sleep(30);
            }catch(Exception e){}
        }

        if (mediaCodec != null) {
            mediaCodec.stop();
            mediaCodec.release();
            mediaCodec = null;
        }

        mMuxerStarted = false;
    }

    public void encode(byte []input) {

        boolean endOfStream = false;
        mIsEncodeing = true;

        final ByteBuffer[] inputBuffers = mediaCodec.getInputBuffers();
        final int inputBufferIndex = mediaCodec.dequeueInputBuffer(TIMEOUT_USEC);

        if (inputBufferIndex >= 0) {
            final ByteBuffer inputBuffer = inputBuffers[inputBufferIndex];
            inputBuffer.clear();
            if (input != null) {
                inputBuffer.put(input);

                mediaCodec.queueInputBuffer(
                        inputBufferIndex,
                        0,
                        input.length,
                        getPTS(),
                        0);//将缓冲区入队
            }else{
                endOfStream = true;

                mediaCodec.queueInputBuffer(
                        inputBufferIndex,
                        0,
                        0,
                        getPTS(),
                        MediaCodec.BUFFER_FLAG_END_OF_STREAM);
            }
        }

        ByteBuffer[] encoderOutputBuffers = mediaCodec.getOutputBuffers();
        int encoderStatus;

        do{
            encoderStatus = mediaCodec.dequeueOutputBuffer(mBufferInfo, TIMEOUT_USEC);

            if (encoderStatus == MediaCodec.INFO_TRY_AGAIN_LATER) {
                if (VERBOSE) Log.d(TAG, "endOfStream=" + endOfStream);
                // no output available yet
                if (!endOfStream) {
                    break;      // out of while
                } else {
                    if (VERBOSE) Log.d(TAG, "no output available, spinning to await EOS");
                }
            } else if (encoderStatus == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                // not expected for an encoder
                encoderOutputBuffers = mediaCodec.getOutputBuffers();
            } else if (encoderStatus == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                // should happen before receiving buffers, and should only happen once
                if (mMuxerStarted) {
                    throw new RuntimeException("format changed twice");
                }
                MediaFormat newFormat = mediaCodec.getOutputFormat();
                Log.d(TAG, "encoder output format changed: " + newFormat);
                mMuxerCore.setMediaFormat(MediaMuxerCore.TRACK_VIDEO, newFormat);
                mMuxerStarted = true;
            } else if (encoderStatus < 0) {
                Log.w(TAG, "unexpected result from encoder.dequeueOutputBuffer: " +
                        encoderStatus);
                // let's ignore it
            } else {
                ByteBuffer encodedData = encoderOutputBuffers[encoderStatus];
                if (encodedData == null) {
                    throw new RuntimeException("encoderOutputBuffer " + encoderStatus +
                            " was null");
                }

                if ((mBufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    // The codec config data was pulled out and fed to the muxer when we got
                    // the INFO_OUTPUT_FORMAT_CHANGED status.  Ignore it.
                    if (VERBOSE) Log.d(TAG, "ignoring BUFFER_FLAG_CODEC_CONFIG");
                    mBufferInfo.size = 0;
                }

                if (mBufferInfo.size != 0) {
                    if (!mMuxerStarted) {
                        throw new RuntimeException("muxer hasn't started");
                    }

                    // adjust the ByteBuffer values to match BufferInfo (not needed?)
                    encodedData.position(mBufferInfo.offset);
                    encodedData.limit(mBufferInfo.offset + mBufferInfo.size);

                    mMuxerCore.addMuxerData(new MediaMuxerCore.MuxerData(
                            MediaMuxerCore.TRACK_VIDEO,
                            encodedData,
                            mBufferInfo));

                    if (VERBOSE) {
                        Log.d(TAG, "sent " + mBufferInfo.size + " bytes to muxer, ts=" +
                                mBufferInfo.presentationTimeUs);
                    }
                }

                mediaCodec.releaseOutputBuffer(encoderStatus, false);

                if ((mBufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    if (!endOfStream) {
                        Log.w(TAG, "reached end of stream unexpectedly");
                    } else {
                        if (VERBOSE) Log.d(TAG, "end of stream reached");
                    }
                    break;      // out of while
                }
            }
        }while(encoderStatus >= 0);

        mIsEncodeing = false;
    }

    private long getPTS() {
        long result = System.nanoTime() / 1000L;
        return result;
    }
}
