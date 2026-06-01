using System;
using System.Runtime.InteropServices;

namespace UMR
{
  public static class Native
  {
    [DllImport("umr", EntryPoint = "umr_begin")]
    public static extern byte UMRBegin(
      ref IntPtr muxer,
      string filename,
      ref IntPtr encoder,
      int audioCodecID,
      int sampleRate,
      long audioBitRate,
      int channels,
      int audioBufferSize,
      int videoCodecID,
      int width,
      int height,
      long videoBitRate
    );
    [DllImport("umr", EntryPoint = "umr_encode_audio")]
    public static extern IntPtr UMREncodeAudio(IntPtr encoder, float[] data);
    [DllImport("umr", EntryPoint = "umr_encode_video")]
    public static extern IntPtr UMREncodeVideo(IntPtr encoder, IntPtr data, long pts);
    [DllImport("umr", EntryPoint = "umr_mux")]
    public static extern byte UMRMux(IntPtr muxer, IntPtr packets);
    [DllImport("umr", EntryPoint = "umr_end")]
    public static extern byte UMREnd(ref IntPtr encoder, ref IntPtr muxer);
  }
}