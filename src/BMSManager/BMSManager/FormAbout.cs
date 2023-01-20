using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;

namespace BMSManager
{
    partial class FormAbout : Form
    {
        public FormAbout()
        {
            InitializeComponent();
        }

        private void FormAbout_Load(object sender, EventArgs e)
        {
            Version ver = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
            String Version = ver.Major.ToString() + "." + ver.Minor.ToString()
                    + "." + ver.Build.ToString() + "." + ver.Revision.ToString();
            lblBMS.Text += " " + Version;
        }
    }
}
