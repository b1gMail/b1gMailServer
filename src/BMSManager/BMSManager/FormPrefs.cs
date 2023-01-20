using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace BMSManager
{
    public partial class FormPrefs : Form
    {
        public FormPrefs()
        {
            InitializeComponent();
        }

        private void buttonBrowseQueueDir_Click(object sender, EventArgs e)
        {
            folderBrowserDialog.SelectedPath = queueDir.Text;
            if (folderBrowserDialog.ShowDialog() == DialogResult.OK)
                queueDir.Text = folderBrowserDialog.SelectedPath;
        }

        private void buttonBrowseLogFile_Click(object sender, EventArgs e)
        {
            try
            {
                fileBrowserDialog.InitialDirectory = System.IO.Path.GetDirectoryName(logFile.Text);
            }
            catch (ArgumentException)
            {
            }

            if (fileBrowserDialog.ShowDialog() == DialogResult.OK)
                logFile.Text = fileBrowserDialog.FileName;
        }

        private void enablePOP3SSL_CheckedChanged(object sender, EventArgs e)
        {
            pop3SSLPort.Enabled = enablePOP3SSL.Checked;
        }

        private void enableIMAPSSL_CheckedChanged(object sender, EventArgs e)
        {
            imapSSLPort.Enabled = enableIMAPSSL.Checked;
        }

        private void enableSMTPSSL_CheckedChanged(object sender, EventArgs e)
        {
            smtpSSLPort.Enabled = enableSMTPSSL.Checked;
        }
    }
}
