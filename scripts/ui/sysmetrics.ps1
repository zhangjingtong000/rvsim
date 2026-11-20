Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class S {
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SystemParametersInfoW(int a, int u, ref NONCLIENTMETRICS n, int f);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct LOGFONT {
    public int lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    public byte lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string lfFaceName;
  }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct NONCLIENTMETRICS {
    public int cbSize, iBorderWidth, iScrollWidth, iScrollHeight, iCaptionWidth, iCaptionHeight;
    public LOGFONT lfCaptionFont;
    public int iSmCaptionWidth, iSmCaptionHeight;
    public LOGFONT lfSmCaptionFont;
    public int iMenuWidth, iMenuHeight;
    public LOGFONT lfMenuFont;
    public LOGFONT lfStatusFont;
    public LOGFONT lfMessageFont;
    public int iPaddedBorderWidth;
  }
  public static NONCLIENTMETRICS Metrics() {
    var n = new NONCLIENTMETRICS();
    n.cbSize = Marshal.SizeOf(typeof(NONCLIENTMETRICS));
    SystemParametersInfoW(0x0029, (uint)n.cbSize, ref n, 0);   // SPI_GETNONCLIENTMETRICS
    return n;
  }
}
"@

$n = [S]::Metrics()
"── Win32 系统度量 (100% DPI) ──"
"SM_CYCAPTION      标题栏高      = " + [S]::GetSystemMetrics(4)
"SM_CXBORDER       边框宽        = " + [S]::GetSystemMetrics(5)
"SM_CYBORDER       边框高        = " + [S]::GetSystemMetrics(6)
"SM_CXSIZEFRAME    可调边框宽    = " + [S]::GetSystemMetrics(32)
"SM_CYSIZEFRAME    可调边框高    = " + [S]::GetSystemMetrics(33)
"SM_CXPADDEDBORDER 额外边框      = " + [S]::GetSystemMetrics(92)
"SM_CXSMICON        小图标       = " + [S]::GetSystemMetrics(49)
"SM_CXICON          大图标       = " + [S]::GetSystemMetrics(11)
"SM_CYMENU          菜单栏高     = " + [S]::GetSystemMetrics(15)
"SM_CXSCROLL        滚动条宽     = " + [S]::GetSystemMetrics(2)
""
"── NONCLIENTMETRICS ──"
"iBorderWidth    = " + $n.iBorderWidth
"iCaptionWidth   = " + $n.iCaptionWidth
"iCaptionHeight  = " + $n.iCaptionHeight
"iPaddedBorder   = " + $n.iPaddedBorderWidth
"iScrollWidth    = " + $n.iScrollWidth
"iMenuHeight     = " + $n.iMenuHeight
""
"── 标题栏字体 ──"
"face   = " + $n.lfCaptionFont.lfFaceName
"height = " + $n.lfCaptionFont.lfHeight + "  (负值 = 字符高度, 正值 = 单元格高度)"
"weight = " + $n.lfCaptionFont.lfWeight
""
"── 菜单字体 ──"
"face   = " + $n.lfMenuFont.lfFaceName
"height = " + $n.lfMenuFont.lfHeight
"weight = " + $n.lfMenuFont.lfWeight
""
"── 消息框字体 ──"
"face   = " + $n.lfMessageFont.lfFaceName
"height = " + $n.lfMessageFont.lfHeight
""
"── 注册表：桌面图标度量 (twips, /15 = px) ──"
$k = "HKCU:\Control Panel\Desktop\WindowMetrics"
foreach ($v in @("IconSpacing","IconVerticalSpacing","CaptionHeight","CaptionWidth","BorderWidth","PaddedBorderWidth","ShellIconSize")) {
  $val = (Get-ItemProperty -Path $k -Name $v -ErrorAction SilentlyContinue).$v
  if ($val -ne $null) { "{0,-22} = {1,-8} -> {2} px" -f $v, $val, [Math]::Round([Math]::Abs([int]$val)/15, 1) }
}
""
"── DWM 圆角 ──"
"DWM_WINDOW_CORNER_PREFERENCE 2 = DWMWCP_ROUND (系统实际绘制 8px)"
