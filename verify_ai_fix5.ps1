# Controlled test: does having rows CHECKED (at 0% CPU) make modal dialogs invisible?
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Q {
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
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
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
function Cpu([int]$tid,[int]$ms=2000){
  $h=[Q]::OpenThread(0x40,$false,[uint32]$tid); $c=0;$e=0;$k=0;$u=0
  [Q]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $a=$k+$u
  Start-Sleep -Milliseconds $ms
  [Q]::GetThreadTimes($h,[ref]$c,[ref]$e,[ref]$k,[ref]$u)|Out-Null; $b=$k+$u
  [Q]::CloseHandle($h)|Out-Null
  return [int][math]::Round((($b-$a)/10000.0)/$ms*100,0)
}
function DumpOwned($owner,$tag){
  $list = New-Object System.Collections.ArrayList
  $cb = [Q+EnumProc]{ param($h,$q)
    if ([Q]::GetWindow($h,4) -eq $owner) {
      $r = New-Object Q+RECT
      [Q]::GetWindowRect($h,[ref]$r) | Out-Null
      $tx = New-Object System.Collections.ArrayList
      $cb2 = [Q+EnumProc]{ param($c,$qq) if ([Q]::Cls($c) -eq 'Static' -and [Q]::Txt($c) -ne '') { [void]$tx.Add([Q]::Txt($c)) }; return $true }
      [Q]::EnumChildWindows($h,$cb2,[IntPtr]::Zero) | Out-Null
      [void]$list.Add("   [$tag] hwnd=$h cls=$([Q]::Cls($h)) title='$([Q]::Txt($h))' visible=$([Q]::IsWindowVisible($h)) rect=$($r.L),$($r.T) $($r.R-$r.L)x$($r.B-$r.T) text='$($tx -join '|')'")
    }
    return $true }
  [Q]::EnumWindows($cb,[IntPtr]::Zero) | Out-Null
  $list | ForEach-Object { $_ }
}
$p = Get-Process MusicPlayer2
$pid_ = [uint32]$p.Id
$main = [IntPtr]$p.MainWindowHandle
$uiTid = 0; [Q]::GetWindowThreadProcessId($main, [ref]$uiTid) | Out-Null

# find the AI dialog: #32770 with title != app caption, owned
$dlg = [IntPtr]::Zero
$cbF = [Q+EnumProc]{ param($h,$q)
  $tp=0; [Q]::GetWindowThreadProcessId($h,[ref]$tp) | Out-Null
  if ($tp -eq $pid_ -and [Q]::Cls($h) -eq '#32770' -and [Q]::Txt($h) -eq 'AI' + [char]0x81EA + [char]0x52A8 + [char]0x6574 + [char]0x7406 + [char]0x6B4C + [char]0x66F2) { $script:dlg = $h }
  return $true }
[Q]::EnumWindows($cbF,[IntPtr]::Zero) | Out-Null
if ($dlg -eq [IntPtr]::Zero) { "AI dialog not found"; exit 1 }
"AI dialog hwnd=$dlg enabled=$([Q]::IsWindowEnabled($dlg))"
"owned windows BEFORE:"
DumpOwned $dlg "before"
$list = [Q]::FindChildCls($dlg, 'SysListView32')
$n = [Q]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
$hProc = [Q]::OpenProcess(0x1F0FFF, $false, $pid_)
$buf = [Q]::VirtualAllocEx($hProc, [IntPtr]::Zero, [IntPtr]128, 0x1000, 0x04)
$arr = New-Object byte[] 128
[BitConverter]::GetBytes([uint32]0x8).CopyTo($arr,0)
[BitConverter]::GetBytes([uint32]0xF000).CopyTo($arr,16)

# ---------- phase A: check 328 rows, click Apply ----------
[BitConverter]::GetBytes([uint32]0x2000).CopyTo($arr,12)
for ($i=0; $i -lt 328; $i++) {
  [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4)
  $w=[IntPtr]::Zero; [Q]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null
  [Q]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null
}
$cpuA = Cpu $uiTid 2500
"PHASE A: 328 rows CHECKED, CPU=${cpuA}%"
$apply = [Q]::FindChild($dlg, 1397)
[Q]::PostMessage($dlg, 0x0111, [IntPtr](1397 -bor (0 -shl 16)), $apply) | Out-Null
Start-Sleep -Milliseconds 1200
"owned windows after Apply (checked):"
DumpOwned $dlg "A"

# dismiss anything that appeared
$cbD = [Q+EnumProc]{ param($h,$q)
  if ([Q]::GetWindow($h,4) -eq $script:dlg) {
    $b = [Q]::FindChild($h,1); if ($b -eq [IntPtr]::Zero) { $b = [Q]::FindChild($h,7) }
    $id = if ([Q]::FindChild($h,1) -ne [IntPtr]::Zero) { 1 } else { 7 }
    [Q]::PostMessage($h,0x0111,[IntPtr]($id -bor (0 -shl 16)),$b)|Out-Null
  }
  return $true }
[Q]::EnumWindows($cbD,[IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1000
"after dismiss: AI dialog enabled=$([Q]::IsWindowEnabled($dlg))"

# ---------- phase B: uncheck all rows, click Apply ----------
[BitConverter]::GetBytes([uint32]0x1000).CopyTo($arr,12)
for ($i=0; $i -lt 328; $i++) {
  [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4)
  $w=[IntPtr]::Zero; [Q]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null
  [Q]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null
}
$cpuB = Cpu $uiTid 2500
"PHASE B: 0 rows checked, CPU=${cpuB}%"
[Q]::PostMessage($dlg, 0x0111, [IntPtr](1397 -bor (0 -shl 16)), $apply) | Out-Null
Start-Sleep -Milliseconds 1200
"owned windows after Apply (unchecked):"
DumpOwned $dlg "B"
[Q]::EnumWindows($cbD,[IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
[Q]::VirtualFreeEx($hProc,$buf,[IntPtr]::Zero,0x8000)|Out-Null
[Q]::CloseHandle($hProc)|Out-Null
