package jerome.com.usbcamera;

import android.media.MediaCodec;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.util.Log;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.ConcurrentLinkedQueue;

public class MediaMuxerCore implements Runnable {

    public enum RecordType {
        RECORD_AUDIO_ONLY,
        RECORD_VIDEO_ONLY,
        RECORD_AUDIO_AND_VIDDEO
    };

    public static final int TRACK_VIDEO = 0;
    public static final int TRACK_AUDIO = 1;

    private MediaMuxer mMuxer;
    private VideoEncoderCore mVideoEncoder = null;
    AudioEncodeCore mAudioEncoder = null;
    private int videoTrackIndex;
    private int audioTrackIndex;
    private RecordType mRecordType = RecordType.RECORD_VIDEO_ONLY;

    private ConcurrentLinkedQueue muxerDatasQueue = new ConcurrentLinkedQueue<MuxerData>();

    private boolean isExit = false;
    private boolean isMediaMuxerStart = false;
    private boolean isRunning = false;
    private final Object mLock = new Object();

    public MediaMuxerCore (int width, int height, RecordType type) throws IOException{

        mRecordType = type;

        if(type != RecordType.RECORD_AUDIO_ONLY) {
            mVideoEncoder = new VideoEncoderCore(this, width, height);
        }

        if(type != RecordType.RECORD_VIDEO_ONLY) {
            mAudioEncoder = new AudioEncodeCore(this);
            mAudioEncoder.startAudioRecord();
        }

        muxerDatasQueue.clear();

        mMuxer = new MediaMuxer(
                FileUtils.getVideoStorageDir() + FileUtils.getVideoDefaultName(),
                MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);

        isRunning = false;
    }

    public boolean isMuxerStarted(){
        return isMediaMuxerStart;
    }

    public void release(){

        if(mAudioEncoder != null) {
            mAudioEncoder.release();
            mAudioEncoder = null;
        }

        if(mVideoEncoder != null){
            mVideoEncoder.encode(null);
            mVideoEncoder.release();
            mVideoEncoder = null;
        }

        isExit = true;
        lockNofify();

        while(isRunning){
            sleepTimes(30);
        }

        if (mMuxer != null) {
            if(isMuxerStarted()){
                mMuxer.stop();
            }
            mMuxer.release();
            mMuxer = null;
        }
        muxerDatasQueue.clear();
        isMediaMuxerStart = false;
    }

    public void addVideoFrameData(byte []data){
        if(mVideoEncoder == null){
            return;
        }

        mVideoEncoder.encode(data);
    }

    public void startMuxer() {
        videoTrackIndex = -1;
        audioTrackIndex = -1;
        isMediaMuxerStart = false;
        new Thread(this).start();
    }

    private void muxerReady(){
        mMuxer.start();
        isMediaMuxerStart = true;
        lockNofify();
    }

    public synchronized void setMediaFormat(int index, MediaFormat mediaFormat) {
        if (mMuxer == null) {
            return;
        }

        if (index == TRACK_VIDEO) {
            videoTrackIndex = mMuxer.addTrack(mediaFormat);
        } else {
            audioTrackIndex = mMuxer.addTrack(mediaFormat);
        }

        switch (mRecordType){
            case RECORD_AUDIO_AND_VIDDEO:
                if(videoTrackIndex != -1 && audioTrackIndex != -1) {
                    muxerReady();
                }
                break;
            case RECORD_AUDIO_ONLY:
                if(audioTrackIndex != -1) {
                    muxerReady();
                }
                break;
            default:
                if(videoTrackIndex != -1) {
                    muxerReady();
                }
                break;
        }
    }

    private void lockNofify(){
        synchronized (mLock) {
            mLock.notify();
        }
    }

    private void lockOn(){
        synchronized (mLock) {
            try {
                mLock.wait();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    private void sleepTimes(int ms){
        try{
            Thread.sleep(ms);
        }catch(Exception e){}
    }

    public void addMuxerData(MuxerData data) {
        if(!isMuxerStarted()){
            return;
        }
        muxerDatasQueue.add(data);
        lockNofify();
    }

    @Override
    public void run() {

        isExit = false;
        isRunning = true;

        while (!isExit) {
            if (isMuxerStarted()) {
                if (muxerDatasQueue.isEmpty()) {
                    lockOn();
                } else {
                    MuxerData data = (MuxerData) muxerDatasQueue.poll();
                    int track;
                    if (data.trackIndex == TRACK_VIDEO) {
                        track = videoTrackIndex;
                    } else {
                        track = audioTrackIndex;
                    }
                    if(track != -1) {
                        try {
                            mMuxer.writeSampleData(track, data.byteBuf, data.bufferInfo);
                        } catch (Exception e) {
                        }
                    }
                }
            } else {
                lockOn();
            }
        }
        isRunning = false;
    }

    public static class MuxerData {
        int trackIndex;
        ByteBuffer byteBuf;
        MediaCodec.BufferInfo bufferInfo;

        public MuxerData(int trackIndex, ByteBuffer byteBuf, MediaCodec.BufferInfo bufferInfo) {
            this.trackIndex = trackIndex;
            this.byteBuf = byteBuf;
            this.bufferInfo = bufferInfo;
        }
    }
}