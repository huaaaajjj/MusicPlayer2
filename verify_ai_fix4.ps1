# Reopen the AI dialog (fresh snapshot, real playlist), check rows, measure CPU, and confirm a modal dialog is VISIBLE.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class R {
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
  [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint a, bool i, uint tid);
  [DllImport("kernel32.dll")] public static extern bool GetThreadTimes(IntPtr h, out long c, out long e, out long k, out long u);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static string Cls(IntPtr h){ var sb=new StringBuilder(256); GetClassNameW(h,sb,256); return sb.ToString(); }
  public static string Txt(IntPtr h){ var sb=new StringBuilder(512); GetWindowTextW(h,sb,512); return sb.ToString(); }
  public static IntPtr FindChildCls(IntPtr root, string cls){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (Cls(c)==cls){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindChild(IntPtr root, int id){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (GetDlgCtrlID(c)==id){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindOwned(IntPtr owner){ IntPtr f=IntPtr.Zero; EnumWindows(delegate(IntPtr h, IntPtr p){ if (GetWindow(h,4)==owner){ f=h; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindDlg(IntPtr skip){ IntPtr f=IntPtr.Zero; EnumWindows(delegate(IntPtr h, IntPtr p){ if (Cls(h)=="#32770" && Txt(h)!="MusicPlayer2" && GetWindow(h,4)!=IntPtr.Zero && h!=skip){ f=h; } return true;}, IntPtr.Zero); return f; }
}
'@
function UiCpu([int]$tid,[int]$ms=3000){
  $h=[R]::OpenThread(0x40,$false,[uint32]$tid); $c=0;$e=0;$k=0;$u=0
  [R]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u) | Out-Null; $a=$k+$u
  Start-Sleep -Milliseconds $ms
  [R]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u) | Out-Null; $b=$k+$u
  [R]::CloseHandle($h) | Out-Null
  return [math]::Round((($b-$a)/10000.0)/$ms*100,0)
}
$p = Get-Process MusicPlayer2
$pid_ = [uint32]$p.Id
$main = [IntPtr]$p.MainWindowHandle
$uiTid = 0; [R]::GetWindowThreadProcessId($main, [ref]$uiTid) | Out-Null

# close existing AI dialog(s): any #32770 whose title is not the app-name caption
$cbKill = [R+EnumProc]{ param($h,$q)
  $tp=0; [R]::GetWindowThreadProcessId($h,[ref]$tp) | Out-Null
  if ($tp -eq $pid_ -and [R]::Cls($h) -eq '#32770' -and [R]::Txt($h) -ne 'MusicPlayer2') { [R]::PostMessage($h,0x0010,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null }
  return $true }
[R]::EnumWindows($cbKill,[IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1200

# reopen the AI dialog -> fresh playlist snapshot
[R]::PostMessage($main, 0x0111, [IntPtr]32899, [IntPtr]::Zero) | Out-Null
$dlg = [IntPtr]::Zero
for ($i=0; $i -lt 20; $i++) {
  Start-Sleep -Milliseconds 500
  $cand = New-Object System.Collections.ArrayList
  $cb = [R+EnumProc]{ param($h,$q)
    $tp=0; [R]::GetWindowThreadProcessId($h,[ref]$tp) | Out-Null
    if ($tp -eq $pid_ -and [R]::Cls($h) -eq '#32770' -and [R]::Txt($h) -ne 'MusicPlayer2') { [void]$cand.Add($h) }
    return $true }
  [R]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
  if ($cand.Count -gt 0) { $dlg = [IntPtr]$cand[0]; break }
}
"AI dialog hwnd=$dlg title='$([R]::Txt($dlg))'"
$list = [R]::FindChildCls($dlg, 'SysListView32')
$n = [R]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
"real playlist rows = $n"
if ($n -eq 0) { "playlist not loaded in this instance - cannot test with real rows"; }

$hProc = [R]::OpenProcess(0x1F0FFF, $false, $pid_)
$buf = [R]::VirtualAllocEx($hProc, [IntPtr]::Zero, [IntPtr]128, 0x1000, 0x04)
$arr = New-Object byte[] 128
[BitConverter]::GetBytes([uint32]0x8).CopyTo($arr,0)
[BitConverter]::GetBytes([uint32]0x2000).CopyTo($arr,12)
[BitConverter]::GetBytes([uint32]0xF000).CopyTo($arr,16)
$toCheck = [math]::Min(328, $n)
for ($i=0; $i -lt $toCheck; $i++) {
  [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4)
  $w=[IntPtr]::Zero
  [R]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w) | Out-Null
  [R]::SendMessage($list,0x102B,[IntPtr]$i,$buf) | Out-Null
}
"checked $toCheck of $n rows"
$cpu = UiCpu $uiTid 3000
">>> UI thread CPU = ${cpu}%   (pre-fix: ~98%)"
[R]::VirtualFreeEx($hProc,$buf,[IntPtr]::Zero,0x8000) | Out-Null
[R]::CloseHandle($hProc) | Out-Null

# modal dialog must be VISIBLE while rows are checked
$apply = [R]::FindChild($dlg, 1397)
[R]::PostMessage($dlg, 0x0111, [IntPtr](1397 -bor (0 -shl 16)), $apply) | Out-Null
Start-Sleep -Milliseconds 1500
$box = [R]::FindOwned($dlg)
if ($box -ne [IntPtr]::Zero) {
  $tx = New-Object System.Collections.ArrayList
  $cbX = [R+EnumProc]{ param($c,$q) if ([R]::Cls($c) -eq 'Static' -and [R]::Txt($c) -ne '') { [void]$tx.Add([R]::Txt($c)) }; return $true }
  [R]::EnumChildWindows($box,$cbX,[IntPtr]::Zero) | Out-Null
  "modal box VISIBLE=$([R]::IsWindowVisible($box))  text: $($tx -join ' | ')"
  $b1 = [R]::FindChild($box,7); $bid = 7
  if ($b1 -eq [IntPtr]::Zero) { $b1 = [R]::FindChild($box,1); $bid = 1 }
  [R]::PostMessage($box, 0x0111, [IntPtr]($bid -bor (0 -shl 16)), $b1) | Out-Null
  Start-Sleep -Milliseconds 700
  "$(if ($bid -eq 7) {'answered NO (nothing modified)'} else {'dismissed'})"
} else { "no modal box appeared" }

# also verify a plain DoModal dialog from the AI dialog is visible
$set = [R]::FindChild($dlg, 1392)
[R]::PostMessage($dlg, 0x0111, [IntPtr](1392 -bor (0 -shl 16)), $set) | Out-Null
Start-Sleep -Milliseconds 1800
$box2 = [R]::FindOwned($dlg)
if ($box2 -ne [IntPtr]::Zero) {
  "settings dialog hwnd=$box2 title='$([R]::Txt($box2))' VISIBLE=$([R]::IsWindowVisible($box2))"
  [R]::PostMessage($box2, 0x0111, [IntPtr]2, [IntPtr]::Zero) | Out-Null   # IDCANCEL
  Start-Sleep -Milliseconds 500
}
"AI dialog enabled at end=$([R]::IsWindowEnabled($dlg))"
