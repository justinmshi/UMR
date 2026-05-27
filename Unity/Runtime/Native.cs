using System;
using System.Runtime.InteropServices;

namespace UMR
{
  public static class Native
  {
    [DllImport("umr_encode", EntryPoint = "umr_encode_begin")]
    public static extern IntPtr UMREncodeBegin(string filename, int codecID, int width, int height, long bitRate);
    [DllImport("umr_encode", EntryPoint = "umr_encode_encode")]
    public static extern byte UMREncodeEncode(IntPtr encoder, IntPtr data, long pts);
    [DllImport("umr_encode", EntryPoint = "umr_encode_end")]
    public static extern byte UMREncodeEnd(ref IntPtr encoder);
  }
}