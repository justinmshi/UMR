using System;
using System.Runtime.InteropServices;

namespace UMR
{
  public static class Native
  {
    [DllImport("umr_encode", EntryPoint = "umr_encode_begin")]
    public static extern IntPtr UMREncodeBegin(
      string filename,
      int videoCodecID,
      int width,
      int height,
      long videoBitRate,
      int audioCodecID,
      int sampleRate,
      long audioBitRate,
      int channels,
      int audioBufferSize
    );
    [DllImport("umr_encode", EntryPoint = "umr_encode_send_video")]
    public static extern byte UMREncodeSendVideo(IntPtr encoder, IntPtr data, long pts);
    [DllImport("umr_encode", EntryPoint = "umr_encode_send_audio")]
    public static extern byte UMREncodeSendAudio(IntPtr encoder, float[] data);
    [DllImport("umr_encode", EntryPoint = "umr_encode_end")]
    public static extern byte UMREncodeEnd(ref IntPtr encoder);
  }
}