# Fresh instance; isolate whether the invisible-modal bug is triggered by
# checked rows (checkbox style) or by the shared CListCtrlEx POSTPAINT SetItemState path
# (which fires whenever IsRowSelected() is true).
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Z {
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
  public static IntPtr FindChildCls(IntPtr root, string cls){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (Cls(c)==cls){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindChild(IntPtr root, int id){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (GetDlgCtrlID(c)==id){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
}
'@
$exe = 'D:\Documents\musicplayer2\MusicPlayer2\x64\Release\MusicPlayer2.exe'
$old = Get-Process MusicPlayer2 -ErrorAction SilentlyContinue
if ($old) { "restarting app"; $h=$old.MainWindowHandle; if ($h -eq 0) { $h = 0 }
  [Z]::PostMessage([IntPtr]$h, 0x0111, [IntPtr]33003, [IntPtr]::Zero) | Out-Null
  for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 500; if (-not (Get-Process MusicPlayer2 -ErrorAction SilentlyContinue)) { break } }
  $s = Get-Process MusicPlayer2 -ErrorAction SilentlyContinue; if ($s) { Stop-Process -Id $s.Id -Force; Start-Sleep -Seconds 2 } }
$p = Start-Process -FilePath $exe -PassThru
for ($i=0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 500; $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break } }
$pid_ = [uint32]$p.Id; $main = [IntPtr]$p.MainWindowHandle
"pid=$pid_ main=$main"
Start-Sleep -Seconds 5   # let the playlist load
[Z]::PostMessage($main, 0x0111, [IntPtr]32899, [IntPtr]::Zero) | Out-Null
$dlg = [IntPtr]::Zero
for ($i=0; $i -lt 20; $i++) { Start-Sleep -Milliseconds 500
  [Z]::EnumWindows([Z+EnumProc]{ param($h,$q)
      $tp=0; [Z]::GetWindowThreadProcessId($h,[ref]$tp)|Out-Null
      if ($tp -eq $pid_ -and [Z]::Cls($h) -eq '#32770' -and [Z]::Txt($h) -ne 'MusicPlayer2') { $script:dlg = $h }
      return $true },[IntPtr]::Zero) | Out-Null
  if ($dlg -ne [IntPtr]::Zero) { break } }
"AI dialog hwnd=$dlg"
$list = [Z]::FindChildCls($dlg, 'SysListView32')
"rows = $([Z]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero))"
$apply = [Z]::FindChild($dlg, 1397)
$hProc = [Z]::OpenProcess(0x1F0FFF, $false, $pid_)
$buf = [Z]::VirtualAllocEx($hProc, [IntPtr]::Zero, [IntPtr]128, 0x1000, 0x04)
$arr = New-Object byte[] 128
[BitConverter]::GetBytes([uint32]0x8).CopyTo($arr,0)

function SetStates([int]$from,[int]$to,[uint32]$state,[uint32]$mask){
  [BitConverter]::GetBytes($state).CopyTo($arr,12)
  [BitConverter]::GetBytes($mask).CopyTo($arr,16)
  for ($i=$from; $i -lt $to; $i++) {
    [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4)
    $w=[IntPtr]::Zero
    [Z]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null
    [Z]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null
  }
}
function TryApply([string]$tag){
  [Z]::PostMessage($dlg,0x0111,[IntPtr](1397 -bor (0 -shl 16)),$apply)|Out-Null
  Start-Sleep -Milliseconds 1300
  $found = New-Object System.Collections.ArrayList
  [Z]::EnumWindows([Z+EnumProc]{ param($h,$q)
      if ([Z]::GetWindow($h,4) -eq $script:dlg) { [void]$found.Add("$h visible=$([Z]::IsWindowVisible($h))") }
      return $true },[IntPtr]::Zero) | Out-Null
  "$tag -> boxes owned: $(if ($found.Count -eq 0) {'NONE'} else { $found -join ' ; ' })"
  # dismiss all owned boxes
  [Z]::EnumWindows([Z+EnumProc]{ param($h,$q)
      if ([Z]::GetWindow($h,4) -eq $script:dlg) {
        $b=[Z]::FindChild($h,1); $id=1
        if ($b -eq [IntPtr]::Zero) { $b=[Z]::FindChild($h,7); $id=7 }
        if ($b -ne [IntPtr]::Zero) { [Z]::PostMessage($h,0x0111,[IntPtr]($id -bor (0 -shl 16)),$b)|Out-Null } }
      return $true },[IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 900
}
$n = [Z]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
$k = [math]::Min(328,$n)

function UiCpu([int]$tid,[int]$ms=2500){
  $h=[Z]::OpenThread(0x40,$false,[uint32]$tid); $c=0;$e=0;$k=0;$u=0
  [Z]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $a=$k+$u
  Start-Sleep -Milliseconds $ms
  [Z]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $b=$k+$u
  [Z]::CloseHandle($h)|Out-Null
  return [int][math]::Round((($b-$a)/10000.0)/$ms*100,0)
}
$uiTid = 0; [Z]::GetWindowThreadProcessId($main, [ref]$uiTid) | Out-Null
TryApply "C) baseline: nothing checked/selected"
SetStates 0 $k 0x2000 0xF000          # CHECK them
$cpuA = UiCpu $uiTid 2500
"A) $k rows CHECKED -> UI thread CPU = ${cpuA}%  (pre-fix ~98%)"
TryApply "A) $k rows CHECKED (checkbox style on)"
SetStates 0 $k 0x1000 0xF000          # uncheck
$ex = [Z]::SendMessage($list,0x1037,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
[Z]::SendMessage($list,0x1036,[IntPtr]::Zero,[IntPtr]($ex -band (-bnot 0x4)))|Out-Null    # remove checkbox style
SetStates 0 $k 0x2 0x2                # SELECT them
TryApply "B) $k rows SELECTED (no checkbox style)"
SetStates 0 $k 0x0 0x2                # deselect
TryApply "D) after deselect"
[Z]::VirtualFreeEx($hProc,$buf,[IntPtr]::Zero,0x8000)|Out-Null
[Z]::CloseHandle($hProc)|Out-Null
