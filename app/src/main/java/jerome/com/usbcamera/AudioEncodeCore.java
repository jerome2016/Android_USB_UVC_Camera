package jerome.com.usbcamera;

import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaRecorder;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;

public class AudioEncodeCore implements Runnable {

    public static final String TAG = "AudioEncodeCore";

    private static final int SAMPLES_PER_FRAME = 1024;
    private static final int FRAMES_PER_BUFFER = 25;
    protected static final int TIMEOUT_USEC = 10000; // 10ms
    private static final String MIME_TYPE = "audio/mp4a-latm";

    private static final int SAMPLE_RATE = 16000;
    private static final int BIT_RATE = 64000;
    private static final int CHANNEL_MASK = AudioFormat.CHANNEL_IN_MONO;

    private MediaCodec mediaCodec; // API >= 16(Android4.1.2)
    private volatile boolean isExit = false;

    private AudioRecord audioRecord;
    private MediaCodec.BufferInfo mBufferInfo; // API >= 16(Android4.1.2)
    private boolean mIsEncodeing = false;
    private boolean mIsStartRecord = false;

    //private long prevOutputPTSUs = 0;

    private MediaMuxerCore mMuxerCore;
    private boolean mMuxerStarted;

    public AudioEncodeCore(MediaMuxerCore muxer) throws IOException {

        mMuxerCore = muxer;
        mBufferInfo = new MediaCodec.BufferInfo();
        mIsStartRecord = false;

        int channelCout = (CHANNEL_MASK == AudioFormat.CHANNEL_IN_MONO) ? 1 : 2;
        MediaFormat audioFormat = MediaFormat.createAudioFormat(MIME_TYPE, SAMPLE_RATE, channelCout);
        audioFormat.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);
        audioFormat.setInteger(MediaFormat.KEY_CHANNEL_MASK, CHANNEL_MASK);
        audioFormat.setInteger(MediaFormat.KEY_BIT_RATE, BIT_RATE);
        audioFormat.setInteger(MediaFormat.KEY_CHANNEL_COUNT, channelCout);
        audioFormat.setInteger(MediaFormat.KEY_SAMPLE_RATE, SAMPLE_RATE);
        mediaCodec = MediaCodec.createEncoderByType(MIME_TYPE);
        mediaCodec.configure(audioFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        mediaCodec.start();
    }

    public void release() {

        isExit = true;

        while(mIsEncodeing){
            try{
                Thread.sleep(100);
            }catch(Exception e){

            }
        }

        if (audioRecord != null) {
            audioRecord.stop();
            audioRecord.release();
            audioRecord = null;
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
            }
        }

        if (mediaCodec != null) {
            mediaCodec.stop();
            mediaCodec.release();
            mediaCodec = null;
        }
        mMuxerStarted = false;
    }

    public boolean isStartRecord(){
        return mIsStartRecord;
    }

    public void startAudioRecord() {

        final int min_buffer_size = AudioRecord.getMinBufferSize(
                SAMPLE_RATE,
                CHANNEL_MASK,
                AudioFormat.ENCODING_PCM_16BIT);

        int buffer_size = SAMPLES_PER_FRAME * FRAMES_PER_BUFFER;

        if (buffer_size < min_buffer_size) {
            buffer_size = ((min_buffer_size / SAMPLES_PER_FRAME) + 1) * SAMPLES_PER_FRAME * 2;
        }

        audioRecord = new AudioRecord(
                MediaRecorder.AudioSource.DEFAULT,
                SAMPLE_RATE,
                CHANNEL_MASK,
                AudioFormat.ENCODING_PCM_16BIT,
                buffer_size);

        audioRecord.startRecording();
        isExit = false;
        new Thread(this).start();
        mIsStartRecord = true;
    }

    @Override
    public void run() {
        final ByteBuffer buf = ByteBuffer.allocateDirect(SAMPLES_PER_FRAME);
        int readBytes;
        mIsEncodeing = true;
        while (!isExit) {
                buf.clear();
                readBytes = audioRecord.read(buf, SAMPLES_PER_FRAME);
                if (readBytes > 0) {
                    buf.position(readBytes);
                    buf.flip();
                    encode(buf, readBytes, getPTS());
                }
        }
	    encode(null, 0, getPTS());
        mIsEncodeing = false;
    }

    private void encode(final ByteBuffer buffer, final int length, final long presentationTimeUs) {
        boolean endOfStream = false;
        final ByteBuffer[] inputBuffers = mediaCodec.getInputBuffers();
        final int inputBufferIndex = mediaCodec.dequeueInputBuffer(TIMEOUT_USEC);

        /*向编码器输入数据*/
        if (inputBufferIndex >= 0) {
            final ByteBuffer inputBuffer = inputBuffers[inputBufferIndex];
            inputBuffer.clear();
            if (buffer != null) {
                inputBuffer.put(buffer);
            }

            if (length <= 0) {
                endOfStream = true;
                mediaCodec.queueInputBuffer(
                        inputBufferIndex,
                        0,
                        0,
                        presentationTimeUs,
                        MediaCodec.BUFFER_FLAG_END_OF_STREAM);
            } else {
                mediaCodec.queueInputBuffer(
                        inputBufferIndex,
                        0,
                        length,
                        presentationTimeUs,
                        0);
            }
        }

        ByteBuffer[] encoderOutputBuffers = mediaCodec.getOutputBuffers();
        int encoderStatus;

        do {
            encoderStatus = mediaCodec.dequeueOutputBuffer(mBufferInfo, TIMEOUT_USEC);

            if (encoderStatus == MediaCodec.INFO_TRY_AGAIN_LATER) {
                if (!endOfStream) {
                    break;      // out of while
                } else {
                    Log.d(TAG, "Audio no output available, spinning to await EOS");
                }
            } else if (encoderStatus == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                encoderOutputBuffers = mediaCodec.getOutputBuffers();
            } else if (encoderStatus == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {

                if (mMuxerStarted) {
                    throw new RuntimeException("Audio format changed twice");
                }
                MediaFormat newFormat = mediaCodec.getOutputFormat();
                Log.d(TAG, "Audio encoder output format changed: " + newFormat);

                // now that we have the Magic Goodies, start the muxer
                mMuxerCore.setMediaFormat(MediaMuxerCore.TRACK_AUDIO, newFormat);
                mMuxerStarted = true;

            } else if (encoderStatus < 0) {

            } else {
                final ByteBuffer encodedData = encoderOutputBuffers[encoderStatus];
                if ((mBufferInfo.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    // You shoud set output format to muxer here when you target Android4.3 or less
                    // but MediaCodec#getOutputFormat can not call here(because INFO_OUTPUT_FORMAT_CHANGED don't come yet)
                    // therefor we should expand and prepare output format from buffer data.
                    // This sample is for API>=18(>=Android 4.3), just ignore this flag here
                    mBufferInfo.size = 0;
                }

                if (mBufferInfo.size != 0) {

                    if (!mMuxerStarted) {
                        throw new RuntimeException("Audio muxer hasn't started");
                    }

                    //mBufferInfo.presentationTimeUs = getPTS();

                    mMuxerCore.addMuxerData(new MediaMuxerCore.MuxerData(
                            MediaMuxerCore.TRACK_AUDIO,
                            encodedData,
                            mBufferInfo));

                    //prevOutputPTSUs = mBufferInfo.presentationTimeUs;
                }
                mediaCodec.releaseOutputBuffer(encoderStatus, false);

                if ((mBufferInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    if (!endOfStream) {
                        Log.w(TAG, "Audio reached end of stream unexpectedly");
                    } else {
                        Log.d(TAG, "Audio end of stream reached");
                    }
                    break;      // out of while
                }
            }
        } while (encoderStatus >= 0);
    }

    private long getPTS() {
        long result = System.nanoTime() / 1000L;
        // presentationTimeUs should be monotonic
        // otherwise muxer fail to write
        //if (result < prevOutputPTSUs) {
        //    result = (prevOutputPTSUs - result) + result;
        //}
        return result;
    }
}
