//***************************************************************************
// Program.cs : 애플리케이션 진입점.
//***************************************************************************

using System;
using System.Windows.Forms;

namespace ChatApp
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.SetHighDpiMode(HighDpiMode.SystemAware);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new ChatClientForm());
        }
    }
}