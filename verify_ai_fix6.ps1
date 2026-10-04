# Is the invisible-modal bug caused by CListCtrlEx's POSTPAINT SetItemState path
# (triggered whenever IsRowSelected() is true - i.e. checked rows for checkbox lists,
#  or selected rows otherwise)?  Test with rows only SELECTED (no checkboxes).
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class S {
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
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] public static extern IntPtr VirtualAllocEx(IntPtr h, IntPtr a, IntPtr s, uint t, uint p);
  [DllImport("kernel32.dll")] public static extern bool WriteProcessMemory(IntPtr h, IntPtr a, byte[] b, IntPtr s, out IntPtr w);
  [DllImport("kernel32.dll")] public static extern bool VirtualFreeEx(IntPtr h, IntPtr a, IntPtr s, uint t);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  public static string Cls(IntPtr h){ var sb=new StringBuilder(256); GetClassNameW(h,sb,256); return sb.ToString(); }
  public static string Txt(IntPtr h){ var sb=new StringBuilder(512); GetWindowTextW(h,sb,512); return sb.ToString(); }
  public static IntPtr FindChildCls(IntPtr root, string cls){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (Cls(c)==cls){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
  public static IntPtr FindChild(IntPtr root, int id){ IntPtr f=IntPtr.Zero; EnumChildWindows(root, delegate(IntPtr c, IntPtr p){ if (GetDlgCtrlID(c)==id){ f=c; return false;} return true;}, IntPtr.Zero); return f; }
}
'@
$p = Get-Process MusicPlayer2
$pid_ = [uint32]$p.Id
$main = [IntPtr]$p.MainWindowHandle
$dlg = [IntPtr]::Zero
$cbF = [S+EnumProc]{ param($h,$q)
  $tp=0; [S]::GetWindowThreadProcessId($h,[ref]$tp) | Out-Null
  if ($tp -eq $pid_ -and [S]::Cls($h) -eq '#32770' -and [S]::Txt($h) -ne 'MusicPlayer2') { $script:dlg = $h }
  return $true }
[S]::EnumWindows($cbF,[IntPtr]::Zero) | Out-Null
"AI dialog hwnd=$dlg"
$list = [S]::FindChildCls($dlg, 'SysListView32')
$hProc = [S]::OpenProcess(0x1F0FFF, $false, $pid_)
$buf = [S]::VirtualAllocEx($hProc, [IntPtr]::Zero, [IntPtr]128, 0x1000, 0x04)
$arr = New-Object byte[] 128
[BitConverter]::GetBytes([uint32]0x8).CopyTo($arr,0)

function Cleanup { [S]::EnumWindows([S+EnumProc]{ param($h,$q)
    if ([S]::GetWindow($h,4) -eq $script:dlg) {
      $b=[S]::FindChild($h,1); $id=1
      if ($b -eq [IntPtr]::Zero) { $b=[S]::FindChild($h,7); $id=7 }
      if ($b -ne [IntPtr]::Zero) { [S]::PostMessage($h,0x0111,[IntPtr]($id -bor (0 -shl 16)),$b)|Out-Null }
    }
    return $true },[IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 700 }

function BoxVisible {
  $vis = -1; $cnt = 0
  [S]::EnumWindows([S+EnumProc]{ param($h,$q)
    if ([S]::GetWindow($h,4) -eq $script:dlg) { $script:cnt++; if ([S]::IsWindowVisible($h)) { $script:vis = 1 } elseif ($script:vis -eq -1) { $script:vis = 0 } }
    return $true },[IntPtr]::Zero) | Out-Null
  return "$(if ($cnt -eq 0) {'NO-BOX'} elseif ($vis -eq 1) {'VISIBLE'} else {'INVISIBLE (zombie)'})" }

# --- state A: checkboxes ON, 328 rows CHECKED ---
Cleanup
[BitConverter]::GetBytes([uint32]0x2000).CopyTo($arr,12)
for ($i=0; $i -lt 328; $i++) { [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4); $w=[IntPtr]::Zero
  [S]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null; [S]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null }
$apply=[S]::FindChild($dlg,1397)
[S]::PostMessage($dlg,0x0111,[IntPtr](1397 -bor (0 -shl 16)),$apply)|Out-Null
Start-Sleep -Milliseconds 1200
"A) 328 rows CHECKED (checkbox style on)  -> box: $(BoxVisible)"

# --- state B: uncheck all, REMOVE checkbox style, SELECT 328 rows ---
Cleanup
[BitConverter]::GetBytes([uint32]0x1000).CopyTo($arr,12)
for ($i=0; $i -lt 328; $i++) { [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4); $w=[IntPtr]::Zero
  [S]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null; [S]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null }
$ex = [S]::SendMessage($list,0x1037,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
[S]::SendMessage($list,0x1036,[IntPtr]::Zero,[IntPtr]($ex -band (-bnot 0x4)))|Out-Null   # clear LVS_EX_CHECKBOXES
# select 328 rows (LVIS_SELECTED = 0x2)
[BitConverter]::GetBytes([uint32]0x2).CopyTo($arr,12)
[BitConverter]::GetBytes([uint32]0x2).CopyTo($arr,16)
for ($i=0; $i -lt 328; $i++) { [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4); $w=[IntPtr]::Zero
  [S]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null; [S]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null }
[S]::PostMessage($dlg,0x0111,[IntPtr](1397 -bor (0 -shl 16)),$apply)|Out-Null
Start-Sleep -Milliseconds 1200
"B) 328 rows SELECTED (no checkbox style) -> box: $(BoxVisible)"

# --- state C: deselect all ---
Cleanup
[BitConverter]::GetBytes([uint32]0x0).CopyTo($arr,12)
[BitConverter]::GetBytes([uint32]0x2).CopyTo($arr,16)
for ($i=0; $i -lt 328; $i++) { [BitConverter]::GetBytes([int32]$i).CopyTo($arr,4); $w=[IntPtr]::Zero
  [S]::WriteProcessMemory($hProc,$buf,$arr,128,[ref]$w)|Out-Null; [S]::SendMessage($list,0x102B,[IntPtr]$i,$buf)|Out-Null }
[S]::PostMessage($dlg,0x0111,[IntPtr](1397 -bor (0 -shl 16)),$apply)|Out-Null
Start-Sleep -Milliseconds 1200
"C) 0 rows selected                     -> box: $(BoxVisible)"
Cleanup
[S]::VirtualFreeEx($hProc,$buf,[IntPtr]::Zero,0x8000)|Out-Null
[S]::CloseHandle($hProc)|Out-Null
