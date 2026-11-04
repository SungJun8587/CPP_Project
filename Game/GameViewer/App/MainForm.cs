//***************************************************************************
// MainForm.cs : 뷰어 창. 타이머로 GameCanvas.Tick() 을 돌린다.
//***************************************************************************
using System;
using System.Drawing;
using System.Windows.Forms;
using GameViewer.Core;
using GameViewer.Core.Data;

namespace GameViewer.App
{
    public sealed class MainForm : Form
    {
        private readonly GameCanvas _canvas;
        private readonly Timer _timer = new Timer();
        private readonly ViewerConfig _cfg;
        private readonly bool _demo;

        public MainForm(ViewerConfig cfg, GameData data, bool demo)
        {
            _cfg = cfg; _demo = demo;
            Text = BuildTitle();
            ClientSize = new Size(cfg.WindowWidth, cfg.WindowHeight);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Color.Black;
            MinimumSize = new Size(640, 480);

            _canvas = new GameCanvas(cfg, data, demo);
            _canvas.Dock = DockStyle.Fill;
            Controls.Add(_canvas);

            _timer.Interval = 16;
            _timer.Tick += delegate { _canvas.Tick(); if ((_ticks++ & 31) == 0) Text = BuildTitle(); };
            _timer.Start();
            Shown += delegate { _canvas.Focus(); };
        }

        private int _ticks;

        private string BuildTitle()
        {
            string where = _demo ? "데모" : _cfg.Host + ":" + _cfg.Port;
            return "Game Viewer - " + where + "  존 " + _cfg.ZoneId + "  캐릭터 " + _cfg.CharId;
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) { _timer.Stop(); _timer.Dispose(); }
            base.Dispose(disposing);
        }
    }
}
