using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using System.Security.Principal;
using Microsoft.Win32;

namespace BMSManager
{
    public partial class FormMain : Form
    {
        public string CfgPath;
        public BMSConfig Cfg;

        public FormMain()
        {
            InitializeComponent();
        }

        private string ServiceStartModeToText(System.ServiceProcess.ServiceStartMode mode)
        {
            switch(mode)
            {
                case System.ServiceProcess.ServiceStartMode.Automatic:
                    return("Automatisch");
                case System.ServiceProcess.ServiceStartMode.Disabled:
                    return("Deaktiviert");
                case System.ServiceProcess.ServiceStartMode.Manual:
                    return("Manuell");
            }

            return ("Unbekannt");
        }

        private string ServiceStatusToText(System.ServiceProcess.ServiceControllerStatus status)
        {
            switch(status)
            {
                case System.ServiceProcess.ServiceControllerStatus.ContinuePending:
                    return("Fortsetzen...");
                case System.ServiceProcess.ServiceControllerStatus.Paused:
                    return("Pausiert");
                case System.ServiceProcess.ServiceControllerStatus.PausePending:
                    return("Pausieren...");
                case System.ServiceProcess.ServiceControllerStatus.Running:
                    return("Gestartet");
                case System.ServiceProcess.ServiceControllerStatus.StartPending:
                    return("Starten...");
                case System.ServiceProcess.ServiceControllerStatus.Stopped:
                    return("Gestoppt");
                case System.ServiceProcess.ServiceControllerStatus.StopPending:
                    return("Stoppen...");
            }

            return ("Unbekannt");
        }

        private delegate void RefreshServiceStatusDelegate();

        private void RefreshServiceStatus()
        {
            foreach(ListViewItem item in listServices.Items)
            {
                string ServiceName = (string)item.Tag;
              
                try
                {
                    BMSServiceController Service = new BMSServiceController(ServiceName);

                    string ServiceStatus = ServiceStatusToText(Service.Status),
                        ServiceMode = ServiceStartModeToText(Service.StartMode);

                    if(ServiceStatus != item.SubItems[1].Text)
                        item.SubItems[1].Text = ServiceStatus;
                    
                    if(ServiceMode != item.SubItems[2].Text)
                        item.SubItems[2].Text = ServiceMode;

                    if (Service.Status == System.ServiceProcess.ServiceControllerStatus.Running
                        && item.ImageIndex != 0)
                        item.ImageIndex = 0;
                    else if (Service.Status == System.ServiceProcess.ServiceControllerStatus.Stopped
                        && item.ImageIndex != 1)
                        item.ImageIndex = 1;
                    else if (Service.Status != System.ServiceProcess.ServiceControllerStatus.Running
                        && Service.Status != System.ServiceProcess.ServiceControllerStatus.Stopped
                        && item.ImageIndex != 2)
                        item.ImageIndex = 2;

                    Service.Close();
                }
                catch(InvalidOperationException)
                {
                    MessageBox.Show("Der zu b1gMailServer zugehörige Dienst \""
                        + ServiceName
                        + "\" wurde nicht gefunden.\n\n"
                        + "Bitte stellen Sie sicher, dass b1gMailServer korrekt installiert ist "
                        + "und versuchen Sie es erneut.",
                        "Dienst nicht gefunden",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                    Application.Exit();
                    return;
                }
            }

            UpdateServiceButtons();
        }

        private void UpdateServiceButtons()
        {
            if(listServices.SelectedItems.Count == 1)
            {
                string ServiceName = (string)listServices.SelectedItems[0].Tag;
                BMSServiceController Service = new BMSServiceController(ServiceName);
                
                toolButtonRestart.Enabled =
                    Service.Status == System.ServiceProcess.ServiceControllerStatus.Running;
                toolButtonStart.Enabled =
                    Service.Status == System.ServiceProcess.ServiceControllerStatus.Stopped;
                toolButtonStop.Enabled =
                    Service.Status == System.ServiceProcess.ServiceControllerStatus.Running;

                Service.Close();
            }
            else
            {
                toolButtonRestart.Enabled = false;
                toolButtonStart.Enabled = false;
                toolButtonStop.Enabled = false;
            }
        }

        private void FormMain_Load(object sender, EventArgs e)
        {
            if(!(new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator)))
            {
                MessageBox.Show("Der b1gMailServer-Manager erfordert Administrator-Rechte.\n\nBitte starten Sie das Programm als Administrator.",
                    "b1gMailServer Manager",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                Application.Exit();
                return;
            }

            string BMSPath = System.IO.Path.GetDirectoryName(Application.ExecutablePath);
            BMSPath = BMSPath.Substring(0, BMSPath.LastIndexOf('\\'));
            CfgPath = BMSPath + "\\b1gmailserver.cfg";
            Cfg = new BMSConfig(CfgPath);
            Cfg.Read();

            if (Cfg.Get("mysql_host") == null)
                Cfg.Set("mysql_host", "localhost");
            if (Cfg.Get("mysql_user") == null)
                Cfg.Set("mysql_user", "root");
            if (Cfg.Get("mysql_pass") == null)
                Cfg.Set("mysql_pass", "");
            if (Cfg.Get("mysql_db") == null)
                Cfg.Set("mysql_db", "b1gmail");
            if (Cfg.Get("queue_dir") == null)
                Cfg.Set("queue_dir", BMSPath + "\\queue");

            RefreshServiceStatus();
            refreshTimer.Enabled = true;

            foreach(string Arg in Environment.GetCommandLineArgs())
                if(Arg == "--firstrun")
                {
                    ShowPrefs(true);
                    Application.Exit();
                    return;
                }
        }

        private void listServices_SelectedIndexChanged(object sender, EventArgs e)
        {
            UpdateServiceButtons();
        }

        private void refreshTimer_Tick(object sender, EventArgs e)
        {
            BeginInvoke(new RefreshServiceStatusDelegate(RefreshServiceStatus));
        }

        private void toolButtonStart_Click(object sender, EventArgs e)
        {
            if (listServices.SelectedItems.Count != 1)
                return;

            BMSServiceController Service = new BMSServiceController((string)listServices.SelectedItems[0].Tag);
            Service.Start();
            Service.Close();

            RefreshServiceStatus();
        }

        private void toolButtonStop_Click(object sender, EventArgs e)
        {
            if (listServices.SelectedItems.Count != 1)
                return;

            BMSServiceController Service = new BMSServiceController((string)listServices.SelectedItems[0].Tag);
            Service.Stop();
            Service.Close();

            RefreshServiceStatus();
        }

        private void toolButtonRestart_Click(object sender, EventArgs e)
        {
            if (listServices.SelectedItems.Count != 1)
                return;

            BMSServiceController Service = new BMSServiceController((string)listServices.SelectedItems[0].Tag);
            Service.Stop();
            Cursor = Cursors.WaitCursor;
            Service.WaitForStatus(System.ServiceProcess.ServiceControllerStatus.Stopped);
            Cursor = Cursors.Default;
            Service.Start();
            Service.Close();

            RefreshServiceStatus();
        }

        private void ShowPrefs(bool FirstRunMode)
        {
            FormPrefs frmPrefs = new FormPrefs();

            if(FirstRunMode)
            {
                frmPrefs.Text = "b1gMailServer-Einstellungen";
                frmPrefs.restartLabel.Visible = false;
                frmPrefs.restartPicture.Visible = false;
                frmPrefs.StartPosition = FormStartPosition.CenterScreen;
                frmPrefs.cancelButton.Enabled = false;
                frmPrefs.ShowInTaskbar = true;
                frmPrefs.ControlBox = false;
            }

            RegistryKey hkBMS = Registry.LocalMachine.OpenSubKey("SOFTWARE\\B1G Software\\b1gMailServer", true);

            if(hkBMS == null)
                hkBMS = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry32).OpenSubKey("SOFTWARE\\B1G Software\\b1gMailServer", true);

            RegistryKey hkPorts = hkBMS.CreateSubKey("Ports"),
                hkSSLPorts = hkBMS.CreateSubKey("SSLPorts"),
                hkInterfaces = hkBMS.CreateSubKey("Interfaces");

            frmPrefs.pop3Port.Value = (int)hkPorts.GetValue("POP3", 110);
            frmPrefs.imapPort.Value = (int)hkPorts.GetValue("IMAP", 143);
            frmPrefs.smtpPort.Value = (int)hkPorts.GetValue("SMTP", 25);
            frmPrefs.httpPort.Value = (int)hkPorts.GetValue("HTTP", 11080);

            frmPrefs.pop3SSLPort.Value = (int)hkSSLPorts.GetValue("POP3", 995);
            frmPrefs.imapSSLPort.Value = (int)hkSSLPorts.GetValue("IMAP", 993);
            frmPrefs.smtpSSLPort.Value = (int)hkSSLPorts.GetValue("SMTP", 465);

            frmPrefs.enablePOP3SSL.Checked = (int)hkSSLPorts.GetValue("EnablePOP3SSL", 0) == 1;
            frmPrefs.enableIMAPSSL.Checked = (int)hkSSLPorts.GetValue("EnableIMAPSSL", 0) == 1;
            frmPrefs.enableSMTPSSL.Checked = (int)hkSSLPorts.GetValue("EnableSMTPSSL", 0) == 1;

            frmPrefs.pop3Address.Text = (string)hkInterfaces.GetValue("POP3", "0.0.0.0");
            frmPrefs.imapAddress.Text = (string)hkInterfaces.GetValue("IMAP", "0.0.0.0");
            frmPrefs.smtpAddress.Text = (string)hkInterfaces.GetValue("SMTP", "0.0.0.0");
            frmPrefs.httpAddress.Text = (string)hkInterfaces.GetValue("HTTP", "0.0.0.0");

            frmPrefs.tcpNoDelay.Checked = (int)hkBMS.GetValue("TCPNoDelay", 0) == 1;
            frmPrefs.listenBacklog.Value = (int)hkBMS.GetValue("ListenBacklog", 128);
            frmPrefs.maxConnections.Value = (int)hkBMS.GetValue("MaxConnections", 100);

            int logLevel = (int)hkBMS.GetValue("LogLevel", 0);
            frmPrefs.logLevel1.Checked = (logLevel & 1) != 0;
            frmPrefs.logLevel2.Checked = (logLevel & 2) != 0;
            frmPrefs.logLevel4.Checked = (logLevel & 4) != 0;
            frmPrefs.logLevel8.Checked = (logLevel & 8) != 0;

            string DefaultLogFileName = System.IO.Path.GetDirectoryName(Application.ExecutablePath);
            DefaultLogFileName = DefaultLogFileName.Substring(0, DefaultLogFileName.LastIndexOf('\\'));
            DefaultLogFileName += "\\BMSService.log";

            frmPrefs.logFile.Text = (string)hkBMS.GetValue("LogFile", DefaultLogFileName);

            frmPrefs.mysqlHost.Text = Cfg.Get("mysql_host");
            frmPrefs.mysqlUser.Text = Cfg.Get("mysql_user");
            frmPrefs.mysqlPassword.Text = Cfg.Get("mysql_pass");
            frmPrefs.mysqlDatabase.Text = Cfg.Get("mysql_db");
            frmPrefs.queueDir.Text = Cfg.Get("queue_dir");
            frmPrefs.disableIPLog.Checked = Cfg.Get("disable_iplog") == "1";

            if(frmPrefs.ShowDialog() == DialogResult.OK)
            {
                hkPorts.SetValue("POP3", frmPrefs.pop3Port.Value, RegistryValueKind.DWord);
                hkPorts.SetValue("IMAP", frmPrefs.imapPort.Value, RegistryValueKind.DWord);
                hkPorts.SetValue("SMTP", frmPrefs.smtpPort.Value, RegistryValueKind.DWord);
                hkPorts.SetValue("HTTP", frmPrefs.httpPort.Value, RegistryValueKind.DWord);

                hkSSLPorts.SetValue("POP3", frmPrefs.pop3SSLPort.Value, RegistryValueKind.DWord);
                hkSSLPorts.SetValue("IMAP", frmPrefs.imapSSLPort.Value, RegistryValueKind.DWord);
                hkSSLPorts.SetValue("SMTP", frmPrefs.smtpSSLPort.Value, RegistryValueKind.DWord);

                hkSSLPorts.SetValue("EnablePOP3SSL", frmPrefs.enablePOP3SSL.Checked ? 1 : 0, RegistryValueKind.DWord);
                hkSSLPorts.SetValue("EnableIMAPSSL", frmPrefs.enableIMAPSSL.Checked ? 1 : 0, RegistryValueKind.DWord);
                hkSSLPorts.SetValue("EnableSMTPSSL", frmPrefs.enableSMTPSSL.Checked ? 1 : 0, RegistryValueKind.DWord);

                hkInterfaces.SetValue("POP3", frmPrefs.pop3Address.Text, RegistryValueKind.String);
                hkInterfaces.SetValue("IMAP", frmPrefs.imapAddress.Text, RegistryValueKind.String);
                hkInterfaces.SetValue("SMTP", frmPrefs.smtpAddress.Text, RegistryValueKind.String);
                hkInterfaces.SetValue("HTTP", frmPrefs.httpAddress.Text, RegistryValueKind.String);

                hkBMS.SetValue("TCPNoDelay", frmPrefs.tcpNoDelay.Checked ? 1 : 0, RegistryValueKind.DWord);
                hkBMS.SetValue("ListenBacklog", frmPrefs.listenBacklog.Value, RegistryValueKind.DWord);
                hkBMS.SetValue("MaxConnections", frmPrefs.maxConnections.Value, RegistryValueKind.DWord);

                logLevel = 0;
                if (frmPrefs.logLevel1.Checked)
                    logLevel |= 1;
                if (frmPrefs.logLevel2.Checked)
                    logLevel |= 2;
                if (frmPrefs.logLevel4.Checked)
                    logLevel |= 4;
                if (frmPrefs.logLevel8.Checked)
                    logLevel |= 8;
                hkBMS.SetValue("LogLevel", logLevel, RegistryValueKind.DWord);
                hkBMS.SetValue("LogFile", frmPrefs.logFile.Text, RegistryValueKind.String);

                Cfg.Set("mysql_host", frmPrefs.mysqlHost.Text);
                Cfg.Set("mysql_user", frmPrefs.mysqlUser.Text);
                Cfg.Set("mysql_pass", frmPrefs.mysqlPassword.Text);
                Cfg.Set("mysql_db", frmPrefs.mysqlDatabase.Text);
                Cfg.Set("queue_dir", frmPrefs.queueDir.Text);
                Cfg.Set("disable_iplog", frmPrefs.disableIPLog.Checked ? "1" : "0");

                Cfg.Write();

                try
                {
                    if(!System.IO.Directory.Exists(frmPrefs.queueDir.Text))
                        System.IO.Directory.CreateDirectory(frmPrefs.queueDir.Text);

                    string[] QueueSubDirs = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                                                "A", "B", "C", "D", "E", "F" };
                    foreach(string SubDir in QueueSubDirs)
                    {
                        string Dir = frmPrefs.queueDir.Text + "\\" + SubDir;
                        if (!System.IO.Directory.Exists(Dir))
                            System.IO.Directory.CreateDirectory(Dir);
                    }
                }
                catch(Exception)
                {
                }
            }
        }

        private void toolButtonPrefs_Click(object sender, EventArgs e)
        {
            ShowPrefs(false);
        }

        private void listServices_DoubleClick(object sender, EventArgs e)
        {
            if (listServices.SelectedItems.Count != 1)
                return;

            string ServiceName = (string)listServices.SelectedItems[0].Tag;
            BMSServiceController Service = new BMSServiceController(ServiceName);

            FormService frmService = new FormService();
            frmService.Text = listServices.SelectedItems[0].Text;

            if (Service.StartMode == System.ServiceProcess.ServiceStartMode.Automatic)
                frmService.startMode.SelectedIndex = 0;
            else if (Service.StartMode == System.ServiceProcess.ServiceStartMode.Manual)
                frmService.startMode.SelectedIndex = 1;
            else if (Service.StartMode == System.ServiceProcess.ServiceStartMode.Disabled)
                frmService.startMode.SelectedIndex = 2;

            if(frmService.ShowDialog() == DialogResult.OK)
            {
                if (frmService.startMode.SelectedIndex == 0)
                    Service.StartMode = System.ServiceProcess.ServiceStartMode.Automatic;
                else if (frmService.startMode.SelectedIndex == 1)
                    Service.StartMode = System.ServiceProcess.ServiceStartMode.Manual;
                else if (frmService.startMode.SelectedIndex == 2)
                    Service.StartMode = System.ServiceProcess.ServiceStartMode.Disabled;

                RefreshServiceStatus();
            }

            Service.Close();
        }

        private void toolButtonAbout_Click(object sender, EventArgs e)
        {
            FormAbout frmAbout = new FormAbout();
            frmAbout.ShowDialog();
        }
    }
}
