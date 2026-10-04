using System;
using System.Collections.Generic;
using System.Drawing;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
class RenderSwitcher {
    [DllImport("user32.dll")] static extern bool DestroyIcon(IntPtr icon);
    [STAThread] static void Main(string[] args) {
        var assembly=Assembly.LoadFrom(args[0]); var type=assembly.GetType("QuickSearch").GetNestedType("Switcher",BindingFlags.NonPublic);
        var panel=(Form)Activator.CreateInstance(type,new object[]{1f});
        var rows=new List<KeyValuePair<IntPtr,string>> {
            new KeyValuePair<IntPtr,string>(IntPtr.Zero,"ChatGPT — white icon contrast preview"),
            new KeyValuePair<IntPtr,string>(IntPtr.Zero,"Visual Studio Code — QuickSearch"),
            new KeyValuePair<IntPtr,string>(IntPtr.Zero,"Browser — documentation")};
        var white=new Bitmap(48,48);using(var g=Graphics.FromImage(white)){g.Clear(Color.Transparent);g.FillEllipse(Brushes.White,6,6,36,36);g.FillEllipse(new SolidBrush(Color.FromArgb(82,91,103)),16,16,16,16);}
        IntPtr whiteIcon=white.GetHicon();
        type.GetField("items",BindingFlags.NonPublic|BindingFlags.Instance).SetValue(panel,rows);
        type.GetField("itemIcons",BindingFlags.NonPublic|BindingFlags.Instance).SetValue(panel,new List<IntPtr>{whiteIcon,SystemIcons.Application.Handle,SystemIcons.Information.Handle});
        panel.ClientSize=new Size(720,240);panel.CreateControl();
        using(var bitmap=new Bitmap(720,240)){panel.DrawToBitmap(bitmap,new Rectangle(0,0,720,240));bitmap.Save(args[1],System.Drawing.Imaging.ImageFormat.Png);}
        panel.Dispose();DestroyIcon(whiteIcon);white.Dispose();
    }
}
