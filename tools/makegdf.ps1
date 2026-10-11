# Builds an Xbox GDF (XDVDFS) disc image from a folder, for gdf2content.
#   .\tools\makegdf.ps1 -Source build\Release\package -Image build\Jellyfin360.gdf
param([Parameter(Mandatory)] [string]$Source, [Parameter(Mandatory)] [string]$Image)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

public static class Gdf
{
    const int Sector = 2048;

    class Node
    {
        public string Name, Path;
        public bool IsDir;
        public long Size;
        public List<Node> Children = new List<Node>();
        public int StartSector;
        public byte[] DirData;
    }

    static Node Scan(string path, string name)
    {
        Node n = new Node { Name = name, Path = path, IsDir = Directory.Exists(path) };
        if (n.IsDir)
        {
            foreach (string d in Directory.GetDirectories(path)) n.Children.Add(Scan(d, System.IO.Path.GetFileName(d)));
            foreach (string f in Directory.GetFiles(path)) n.Children.Add(Scan(f, System.IO.Path.GetFileName(f)));
            n.Children.Sort((a, b) => string.Compare(a.Name, b.Name, StringComparison.OrdinalIgnoreCase));
        }
        else n.Size = new FileInfo(path).Length;
        return n;
    }

    static int EntrySize(Node c) { return (14 + Encoding.ASCII.GetByteCount(c.Name) + 3) & ~3; }

    // Directory table: entries sorted by name and linked only through "right" pointers,
    // which is a valid (if unbalanced) binary search tree. Entries never cross a sector.
    static void LayoutDir(Node dir)
    {
        var offsets = new List<int>();
        int pos = 0;
        foreach (Node c in dir.Children)
        {
            int size = EntrySize(c);
            if (pos / Sector != (pos + size - 1) / Sector) pos = (pos / Sector + 1) * Sector;
            offsets.Add(pos);
            pos += size;
        }
        int total = Math.Max(Sector, (pos + Sector - 1) / Sector * Sector);
        dir.DirData = new byte[total];
        for (int i = 0; i < total; i++) dir.DirData[i] = 0xFF;
        dir.Size = total;
        for (int i = 0; i < dir.Children.Count; i++)
        {
            Node c = dir.Children[i];
            int o = offsets[i];
            int right = i + 1 < dir.Children.Count ? offsets[i + 1] / 4 : 0;
            byte[] name = Encoding.ASCII.GetBytes(c.Name);
            BitConverter.GetBytes((ushort)0).CopyTo(dir.DirData, o);
            BitConverter.GetBytes((ushort)right).CopyTo(dir.DirData, o + 2);
            // start sector and size are filled in once files are placed
            dir.DirData[o + 12] = (byte)(c.IsDir ? 0x10 : 0x80);
            dir.DirData[o + 13] = (byte)name.Length;
            name.CopyTo(dir.DirData, o + 14);
            for (int k = o + 14 + name.Length; k < o + EntrySize(c); k++) dir.DirData[k] = 0;
        }
        dir.Children.ForEach(c => { if (c.IsDir) LayoutDir(c); });
        dirOffsets[dir] = offsets;
    }

    static Dictionary<Node, List<int>> dirOffsets = new Dictionary<Node, List<int>>();

    static void Place(Node n, ref int next)
    {
        n.StartSector = next;
        next += (int)((n.Size + Sector - 1) / Sector);
        if (n.Size == 0) next += 0;
        foreach (Node c in n.Children) Place(c, ref next);
    }

    static void Fill(Node dir)
    {
        List<int> offsets = dirOffsets[dir];
        for (int i = 0; i < dir.Children.Count; i++)
        {
            Node c = dir.Children[i];
            BitConverter.GetBytes((uint)c.StartSector).CopyTo(dir.DirData, offsets[i] + 4);
            BitConverter.GetBytes((uint)c.Size).CopyTo(dir.DirData, offsets[i] + 8);
            if (c.IsDir) Fill(c);
        }
    }

    static void Write(Node n, FileStream fs)
    {
        fs.Seek((long)n.StartSector * Sector, SeekOrigin.Begin);
        if (n.IsDir) fs.Write(n.DirData, 0, n.DirData.Length);
        else using (FileStream src = File.OpenRead(n.Path)) src.CopyTo(fs);
        foreach (Node c in n.Children) Write(c, fs);
    }

    public static string Build(string source, string image)
    {
        Node root = Scan(source, "");
        LayoutDir(root);
        int next = 33;                         // after the volume descriptor at sector 32
        Place(root, ref next);
        Fill(root);
        using (FileStream fs = File.Create(image))
        {
            byte[] vd = new byte[Sector];
            byte[] magic = Encoding.ASCII.GetBytes("MICROSOFT*XBOX*MEDIA");
            magic.CopyTo(vd, 0);
            BitConverter.GetBytes((uint)root.StartSector).CopyTo(vd, 20);
            BitConverter.GetBytes((uint)root.Size).CopyTo(vd, 24);
            BitConverter.GetBytes(DateTime.UtcNow.ToFileTimeUtc()).CopyTo(vd, 28);
            magic.CopyTo(vd, 0x7EC);
            fs.Seek(32L * Sector, SeekOrigin.Begin);
            fs.Write(vd, 0, vd.Length);
            Write(root, fs);
            long end = (long)next * Sector;
            if (fs.Length < end) fs.SetLength(end);
        }
        return string.Format("{0}: {1} sectors", Path.GetFileName(image), next);
    }
}
'@
[Gdf]::Build((Resolve-Path $Source).Path, [IO.Path]::GetFullPath($Image))
