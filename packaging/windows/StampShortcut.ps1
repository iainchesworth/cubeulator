# Stamps a Windows shortcut (.lnk) with an AppUserModelID.
#
# The taskbar groups a window with the shortcut that launched it by matching
# the process's explicit AppUserModelID (set in desktop_integration.cpp)
# against the shortcut's System.AppUserModel.ID property. Without this, a
# shortcut pinned from the Start menu and the running window show as two
# buttons.
#
# Run by the NSIS installer after it creates the Start menu shortcut. Needs
# nothing but Windows PowerShell; no module, no elevation beyond the
# installer's own.
param(
    [Parameter(Mandatory = $true)][string]$Shortcut,
    [Parameter(Mandatory = $true)][string]$AppUserModelId
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class CubeulatorShortcutAumid
{
    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    private class ShellLink { }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
     Guid("0000010B-0000-0000-C000-000000000046")]
    private interface IPersistFile
    {
        void GetClassID(out Guid classId);
        [PreserveSig] int IsDirty();
        void Load([MarshalAs(UnmanagedType.LPWStr)] string fileName, uint mode);
        void Save([MarshalAs(UnmanagedType.LPWStr)] string fileName, bool remember);
        void SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string fileName);
        void GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string fileName);
    }

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    private struct PropertyKey
    {
        public Guid FormatId;
        public uint PropertyId;
    }

    [StructLayout(LayoutKind.Explicit, Size = 24)]
    private struct PropVariant
    {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(8)] public IntPtr Pointer;
    }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
     Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
    private interface IPropertyStore
    {
        void GetCount(out uint count);
        void GetAt(uint index, out PropertyKey key);
        void GetValue(ref PropertyKey key, out PropVariant value);
        void SetValue(ref PropertyKey key, ref PropVariant value);
        void Commit();
    }

    [DllImport("ole32.dll")]
    private static extern int PropVariantClear(ref PropVariant value);

    // System.AppUserModel.ID: {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5.
    private static readonly Guid AppUserModelFormat = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3");
    private const uint AppUserModelIdProperty = 5;
    private const ushort VT_LPWSTR = 31;
    private const uint STGM_READWRITE = 2;

    public static void Set(string path, string appUserModelId)
    {
        object link = new ShellLink();
        ((IPersistFile)link).Load(path, STGM_READWRITE);

        var key = new PropertyKey { FormatId = AppUserModelFormat, PropertyId = AppUserModelIdProperty };
        var value = new PropVariant { Type = VT_LPWSTR, Pointer = Marshal.StringToCoTaskMemUni(appUserModelId) };
        try
        {
            var store = (IPropertyStore)link;
            store.SetValue(ref key, ref value);
            store.Commit();
        }
        finally
        {
            PropVariantClear(ref value);
        }

        ((IPersistFile)link).Save(path, true);
    }

    public static string Get(string path)
    {
        object link = new ShellLink();
        ((IPersistFile)link).Load(path, 0);
        var key = new PropertyKey { FormatId = AppUserModelFormat, PropertyId = AppUserModelIdProperty };
        PropVariant value;
        ((IPropertyStore)link).GetValue(ref key, out value);
        try
        {
            return value.Type == VT_LPWSTR ? Marshal.PtrToStringUni(value.Pointer) : "";
        }
        finally
        {
            PropVariantClear(ref value);
        }
    }
}
'@

if (-not (Test-Path -LiteralPath $Shortcut)) {
    Write-Error "No such shortcut: $Shortcut"
}

[CubeulatorShortcutAumid]::Set($Shortcut, $AppUserModelId)
Write-Output ("Stamped {0} with {1}" -f $Shortcut, [CubeulatorShortcutAumid]::Get($Shortcut))
