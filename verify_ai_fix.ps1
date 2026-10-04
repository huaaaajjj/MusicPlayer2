# End-to-end verification: after checking rows in the AI dialog list,
# (a) the UI thread must NOT peg at ~100%, and (b) a modal dialog must be VISIBLE.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class V {
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
  public static IntPtr FindChild(IntPtr root, int id){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (GetDlgCtrlID(c)==id){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindChildCls(IntPtr root, string cls){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (Cls(c)==cls){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindTopByPid(uint pid, string cls){ IntPtr f=IntPtr.Zero; EnumWindows(delegate(IntPtr h, IntPtr p){ uint q; GetWindowThreadProcessId(h, out q); if (q==pid && Cls(h)==cls){ f=h; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindOwned(IntPtr owner){ IntPtr f=IntPtr.Zero; EnumWindows(delegate(IntPtr h, IntPtr p){ if (GetWindow(h,4)==owner){ f=h; return false;} return true;}, IntPtr.Zero); return f; }
}
'@

function UiCpu([int]$tid,[int]$ms=3000){
  $h=[V]::OpenThread(0x40,$false,[uint32]$tid); $c=0;$e=0;$k=0;$u=0
  [V]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $a=$k+$u
  Start-Sleep -Milliseconds $ms
  [V]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $b=$k+$u
  [V]::CloseHandle($h); return [math]::Round((($b-$a)/10000.0)/$ms*100,0)
}

$exe = 'D:\Documents\musicplayer2\MusicPlayer2\x64\Release\MusicPlayer2.exe'

# ---- 1. close the OLD (pre-fix) instance ----
$old = Get-Process MusicPlayer2 -ErrorAction SilentlyContinue
if ($old) {
  "1) closing old instance pid=$($old.Id)"
  $h = $old.MainWindowHandle
  if ($h -eq 0) { $h = [V]::FindTopByPid([uint32]$old.Id, 'MusicPlayer_l3gwYT') }
  [V]::PostMessage([IntPtr]$h, 0x0111, [IntPtr]33003, [IntPtr]::Zero) | Out-Null   # ID_MENU_EXIT
  for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 500; if (-not (Get-Process MusicPlayer2 -ErrorAction SilentlyContinue)) { break } }
  $still = Get-Process MusicPlayer2 -ErrorAction SilentlyContinue
  if ($still) { "   did not exit gracefully; force stopping"; Stop-Process -Id $still.Id -Force; Start-Sleep -Seconds 2 }
  "   old instance gone"
} else { "1) no running instance" }

# ---- 2. start the new build ----
"2) starting new build"
$p = Start-Process -FilePath $exe -PassThru
for ($i=0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break } }
"   pid=$($p.Id) mainHwnd=$($p.MainWindowHandle)"
$main = [IntPtr]$p.MainWindowHandle
$pid_ = [uint32]$p.Id
$uiTid = 0; [V]::GetWindowThreadProcessId($main, [ref]$uiTid) | Out-Null
"   UI thread=$uiTid"

# ---- 3. open the AI dialog ----
[V]::PostMessage($main, 0x0111, [IntPtr]32899, [IntPtr]::Zero) | Out-Null   # ID_TOOL_AI_ORGANIZE
$dlg = [IntPtr]::Zero
for ($i=0; $i -lt 20; $i++) { Start-Sleep -Milliseconds 500; $dlg = [V]::FindTopByPid($pid_, '#32770'); if ($dlg -ne [IntPtr]::Zero) { break } }
"3) AI dialog hwnd=$dlg title='$([V]::Txt($dlg))' visible=$([V]::IsWindowVisible($dlg))"
$list = [V]::FindChildCls($dlg, 'SysListView32')
$n = [V]::SendMessage($list, 0x1004, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()   # LVM_GETITEMCOUNT
"   list hwnd=$list rows=$n"
$ex = [V]::SendMessage($list, 0x1037, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64()   # LVM_GETEXTENDEDLISTVIEWSTYLE
"   LVS_EX=0x{0:X}  has_CHECKBOXES={1}" -f $ex, (($ex -band 0x4) -ne 0)

# ---- 4. baseline CPU ----
"4) baseline (0 rows checked): UI thread CPU = $(UiCpu $uiTid 2000)%"

# ---- 5. check 328 rows ----
$hProc = [V]::OpenProcess(0x1F0FFF, $false, $pid_)
$buf = [V]::VirtualAllocEx($hProc, [IntPtr]::Zero, [IntPtr]128, 0x1000, 0x04)
$arr = New-Object byte[] 128
[BitConverter]::GetBytes([uint32]0x8).CopyTo($arr,0)       # LVIF_STATE
[BitConverter]::GetBytes([uint32]0x2000).CopyTo($arr,12)   # checked
[BitConverter]::GetBytes([uint32]0xF000).CopyTo($arr,16)   # LVIS_STATEIMAGEMASK
$checked = [math]::Min(328, $n)
for ($i=0; $i -lt $checked; $i++) {
  [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4)
  $w=[IntPtr]::Zero
  [V]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w) | Out-Null
  [V]::SendMessage($list,0x102B,[IntPtr]$i,$buf) | Out-Null
}
"5) checked $checked rows"
$cpu = UiCpu $uiTid 3000
"   >>> UI thread CPU with $checked rows checked = ${cpu}%   (before fix: ~98%)"
[V]::VirtualFreeEx($hProc,$buf,[IntPtr]::Zero,0x8000) | Out-Null
[V]::CloseHandle($hProc) | Out-Null

# ---- 6. modal dialog must be visible while rows are checked ----
$apply = [V]::FindChild($dlg, 1397)
[V]::PostMessage($dlg, 0x0111, [IntPtr](1397 -bor (0 -shl 16)), $apply) | Out-Null
Start-Sleep -Milliseconds 1500
$box = [V]::FindOwned($dlg)
"6) modal child after clicking Apply: hwnd=$box exists=$([V]::IsWindow($box)) VISIBLE=$([V]::IsWindowVisible($box))"
if ($box -ne [IntPtr]::Zero) {
  $tx = New-Object System.Collections.ArrayList
  $cb = [V+EnumProc]{ param($c,$p) if ([V]::Cls($c) -eq 'Static' -and [V]::Txt($c) -ne '') { [void]$tx.Add([V]::Txt($c)) }; return $true }
  [V]::EnumChildWindows($box,$cb,[IntPtr]::Zero) | Out-Null
  "   text: $($tx -join ' | ')"
  "   AI dialog enabled during modal=$([V]::IsWindowEnabled($dlg))  (expect False)"
  $no = [V]::FindChild($box, 7)
  $id = 7
  if ($no -eq [IntPtr]::Zero) { $no = [V]::FindChild($box, 1); $id = 1 }
  [V]::PostMessage($box, 0x0111, [IntPtr]($id -bor (0 -shl 16)), $no) | Out-Null
  Start-Sleep -Milliseconds 600
  "   box dismissed; AI dialog enabled=$([V]::IsWindowEnabled($dlg))  (expect True)"
}
"PID=$($p.Id)"
