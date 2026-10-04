# Decisive test: insert N rows into the AI dialog's list, check them all, measure the UI thread CPU.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class T {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint c);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr a, IntPtr s, uint t, uint p);
  [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr s, out IntPtr w);
  [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr h, IntPtr a, IntPtr s, uint t);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint a, bool i, uint tid);
  [DllImport("kernel32.dll")] public static extern bool GetThreadTimes(IntPtr h, out long c, out long e, out long k, out long u);
  public static string Cls(IntPtr h){ var sb=new StringBuilder(256); GetClassNameW(h,sb,256); return sb.ToString(); }
  public static string Txt(IntPtr h){ var sb=new StringBuilder(512); GetWindowTextW(h,sb,512); return sb.ToString(); }
  public static IntPtr FindTopByPid(uint pid){ IntPtr f=IntPtr.Zero; EnumWindows(delegate(IntPtr h, IntPtr p){ uint q; GetWindowThreadProcessId(h, out q); if (q==pid && Cls(h)=="#32770"){ f=h; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindChildCls(IntPtr root, string cls){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (Cls(c)==cls){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindChild(IntPtr root, int id){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (GetDlgCtrlID(c)==id){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindOwned(IntPtr owner){ IntPtr f=IntPtr.Zero; EnumWindows(delegate(IntPtr h, IntPtr p){ if (GetWindow(h,4)==owner){ f=h; return false;} return true;}, IntPtr.Zero); return f; }
}
'@
function UiCpu([int]$tid,[int]$ms=3000){
  $h=[T]::OpenThread(0x40,$false,[uint32]$tid); $c=0;$e=0;$k=0;$u=0
  [T]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $a=$k+$u
  Start-Sleep -Milliseconds $ms
  [T]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $b=$k+$u
  [T]::CloseHandle($h); return [math]::Round((($b-$a)/10000.0)/$ms*100,0)
}
$p = Get-Process MusicPlayer2
$pid_ = [uint32]$p.Id
$main = [IntPtr]$p.MainWindowHandle
$uiTid = 0; [T]::GetWindowThreadProcessId($main, [ref]$uiTid) | Out-Null
$dlg = [T]::FindTopByPid($pid_)
"pid=$pid_ main=$main uiThread=$uiTid"
"AI dialog hwnd=$dlg title='$([T]::Txt($dlg))' enabled=$([T]::IsWindowEnabled($dlg)) visible=$([T]::IsWindowVisible($dlg))"

# any leftover modal box?
$box = [T]::FindOwned($dlg)
if ($box -ne [IntPtr]::Zero) {
  "leftover modal box hwnd=$box visible=$([T]::IsWindowVisible($box)) - dismissing"
  $b = [T]::FindChild($box,1); if ($b -eq [IntPtr]::Zero) { $b = [T]::FindChild($box,2) }
  [T]::PostMessage($box, 0x0111, [IntPtr]1, $b) | Out-Null
  Start-Sleep -Milliseconds 800
  $box = [T]::FindOwned($dlg)
  "after dismiss: box exists=$([T]::IsWindow($box)) AI dialog enabled=$([T]::IsWindowEnabled($dlg))"
}

$list = [T]::FindChildCls($dlg, 'SysListView32')
$N = 300
"inserting $N rows into list hwnd=$list"
$hProc = [T]::OpenProcess(0x1F0FFF, $false, $pid_)
$buf = [T]::VirtualAllocEx($hProc, [IntPtr]::Zero, [IntPtr]512, 0x1000, 0x04)
$strAddr = [IntPtr]($buf.ToInt64() + 256)
$arr = New-Object byte[] 512
[BitConverter]::GetBytes([uint32]0x1).CopyTo($arr,0)          # LVIF_TEXT
[BitConverter]::GetBytes([int32]16).CopyTo($arr,32)           # cchTextMax
[BitConverter]::GetBytes([int64]$strAddr.ToInt64()).CopyTo($arr,24)   # pszText
for ($i=0; $i -lt $N; $i++) {
  [BitConverter]::GetBytes([int32]0).CopyTo($arr,4)           # iItem (always insert at 0 -> appended)
  $txt = [Text.Encoding]::Unicode.GetBytes("row $i".PadRight(16, " "))
  [Array]::Copy($txt, 0, $arr, 256, $txt.Length)
  $w=[IntPtr]::Zero
  [T]::WriteProcessMemory($hProc,$buf,$arr,512,[ref]$w) | Out-Null
  [T]::SendMessage($list,0x104D,[IntPtr]0,$buf) | Out-Null    # LVM_INSERTITEMW
}
$cnt = [T]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
"list rows now = $cnt"
"UI CPU after inserting rows (0 checked) = $(UiCpu $uiTid 2000)%"

# check them all
$arr2 = New-Object byte[] 128
[BitConverter]::GetBytes([uint32]0x8).CopyTo($arr2,0)
[BitConverter]::GetBytes([uint32]0x2000).CopyTo($arr2,12)
[BitConverter]::GetBytes([uint32]0xF000).CopyTo($arr2,16)
$w2=[IntPtr]::Zero
for ($i=0; $i -lt $N; $i++) {
  [BitConverter]::GetBytes([int32]$i).CopyTo($arr2,4)
  [T]::WriteProcessMemory($hProc,$buf,$arr2,128,[ref]$w2) | Out-Null
  [T]::SendMessage($list,0x102B,[IntPtr]$i,$buf) | Out-Null   # LVM_SETITEMSTATE
}
"checked $N rows"
$cpu = UiCpu $uiTid 3000
">>> UI CPU with $N rows checked = ${cpu}%   (pre-fix this was ~98%)"
[T]::VirtualFreeEx($hProc,$buf,[IntPtr]::Zero,0x8000) | Out-Null
[T]::CloseHandle($hProc) | Out-Null
