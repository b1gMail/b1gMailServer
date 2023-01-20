namespace BMSManager
{
    partial class FormMain
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            System.Windows.Forms.ListViewItem listViewItem1 = new System.Windows.Forms.ListViewItem(new string[] {
            "POP3",
            "",
            ""}, -1);
            System.Windows.Forms.ListViewItem listViewItem2 = new System.Windows.Forms.ListViewItem(new string[] {
            "IMAP",
            "",
            ""}, -1);
            System.Windows.Forms.ListViewItem listViewItem3 = new System.Windows.Forms.ListViewItem(new string[] {
            "SMTP",
            "",
            ""}, -1);
            System.Windows.Forms.ListViewItem listViewItem4 = new System.Windows.Forms.ListViewItem(new string[] {
            "HTTP",
            "",
            ""}, -1);
            System.Windows.Forms.ListViewItem listViewItem5 = new System.Windows.Forms.ListViewItem(new string[] {
            "E-Mail-Warteschleife",
            "",
            ""}, -1);
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(FormMain));
            this.toolStripContainer = new System.Windows.Forms.ToolStripContainer();
            this.statusStrip = new System.Windows.Forms.StatusStrip();
            this.statusLabel = new System.Windows.Forms.ToolStripStatusLabel();
            this.listServices = new System.Windows.Forms.ListView();
            this.columnService = new System.Windows.Forms.ColumnHeader();
            this.columnStatus = new System.Windows.Forms.ColumnHeader();
            this.columnActive = new System.Windows.Forms.ColumnHeader();
            this.imageList = new System.Windows.Forms.ImageList(this.components);
            this.toolStripService = new System.Windows.Forms.ToolStrip();
            this.toolButtonStart = new System.Windows.Forms.ToolStripButton();
            this.toolButtonStop = new System.Windows.Forms.ToolStripButton();
            this.toolButtonRestart = new System.Windows.Forms.ToolStripButton();
            this.toolStripSeparator1 = new System.Windows.Forms.ToolStripSeparator();
            this.toolButtonPrefs = new System.Windows.Forms.ToolStripButton();
            this.toolButtonAbout = new System.Windows.Forms.ToolStripButton();
            this.refreshTimer = new System.Windows.Forms.Timer(this.components);
            this.toolStripContainer.BottomToolStripPanel.SuspendLayout();
            this.toolStripContainer.ContentPanel.SuspendLayout();
            this.toolStripContainer.TopToolStripPanel.SuspendLayout();
            this.toolStripContainer.SuspendLayout();
            this.statusStrip.SuspendLayout();
            this.toolStripService.SuspendLayout();
            this.SuspendLayout();
            // 
            // toolStripContainer
            // 
            // 
            // toolStripContainer.BottomToolStripPanel
            // 
            this.toolStripContainer.BottomToolStripPanel.Controls.Add(this.statusStrip);
            // 
            // toolStripContainer.ContentPanel
            // 
            this.toolStripContainer.ContentPanel.Controls.Add(this.listServices);
            this.toolStripContainer.ContentPanel.Size = new System.Drawing.Size(612, 432);
            this.toolStripContainer.Dock = System.Windows.Forms.DockStyle.Fill;
            this.toolStripContainer.Location = new System.Drawing.Point(0, 0);
            this.toolStripContainer.Name = "toolStripContainer";
            this.toolStripContainer.Size = new System.Drawing.Size(612, 479);
            this.toolStripContainer.TabIndex = 0;
            this.toolStripContainer.Text = "toolStripContainer1";
            // 
            // toolStripContainer.TopToolStripPanel
            // 
            this.toolStripContainer.TopToolStripPanel.Controls.Add(this.toolStripService);
            // 
            // statusStrip
            // 
            this.statusStrip.Dock = System.Windows.Forms.DockStyle.None;
            this.statusStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.statusLabel});
            this.statusStrip.Location = new System.Drawing.Point(0, 0);
            this.statusStrip.Name = "statusStrip";
            this.statusStrip.Size = new System.Drawing.Size(612, 22);
            this.statusStrip.TabIndex = 1;
            // 
            // statusLabel
            // 
            this.statusLabel.Name = "statusLabel";
            this.statusLabel.Size = new System.Drawing.Size(37, 17);
            this.statusLabel.Text = "Bereit";
            // 
            // listServices
            // 
            this.listServices.Columns.AddRange(new System.Windows.Forms.ColumnHeader[] {
            this.columnService,
            this.columnStatus,
            this.columnActive});
            this.listServices.Dock = System.Windows.Forms.DockStyle.Fill;
            this.listServices.FullRowSelect = true;
            this.listServices.GridLines = true;
            this.listServices.HeaderStyle = System.Windows.Forms.ColumnHeaderStyle.Nonclickable;
            listViewItem1.Tag = "b1gMailServer POP3 service";
            listViewItem2.Tag = "b1gMailServer IMAP service";
            listViewItem3.Tag = "b1gMailServer SMTP service";
            listViewItem4.Tag = "b1gMailServer HTTP service";
            listViewItem5.Tag = "b1gMailServer message queue";
            this.listServices.Items.AddRange(new System.Windows.Forms.ListViewItem[] {
            listViewItem1,
            listViewItem2,
            listViewItem3,
            listViewItem4,
            listViewItem5});
            this.listServices.Location = new System.Drawing.Point(0, 0);
            this.listServices.MultiSelect = false;
            this.listServices.Name = "listServices";
            this.listServices.Size = new System.Drawing.Size(612, 432);
            this.listServices.SmallImageList = this.imageList;
            this.listServices.TabIndex = 1;
            this.listServices.UseCompatibleStateImageBehavior = false;
            this.listServices.View = System.Windows.Forms.View.Details;
            this.listServices.SelectedIndexChanged += new System.EventHandler(this.listServices_SelectedIndexChanged);
            this.listServices.DoubleClick += new System.EventHandler(this.listServices_DoubleClick);
            // 
            // columnService
            // 
            this.columnService.Text = "Dienst";
            this.columnService.Width = 360;
            // 
            // columnStatus
            // 
            this.columnStatus.Text = "Status";
            this.columnStatus.Width = 120;
            // 
            // columnActive
            // 
            this.columnActive.Text = "Starttyp";
            this.columnActive.Width = 120;
            // 
            // imageList
            // 
            this.imageList.ImageStream = ((System.Windows.Forms.ImageListStreamer)(resources.GetObject("imageList.ImageStream")));
            this.imageList.TransparentColor = System.Drawing.Color.Transparent;
            this.imageList.Images.SetKeyName(0, "Check.png");
            this.imageList.Images.SetKeyName(1, "Delete.png");
            this.imageList.Images.SetKeyName(2, "Time.png");
            // 
            // toolStripService
            // 
            this.toolStripService.Dock = System.Windows.Forms.DockStyle.None;
            this.toolStripService.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.toolButtonStart,
            this.toolButtonStop,
            this.toolButtonRestart,
            this.toolStripSeparator1,
            this.toolButtonPrefs,
            this.toolButtonAbout});
            this.toolStripService.LayoutStyle = System.Windows.Forms.ToolStripLayoutStyle.HorizontalStackWithOverflow;
            this.toolStripService.Location = new System.Drawing.Point(3, 0);
            this.toolStripService.Name = "toolStripService";
            this.toolStripService.Size = new System.Drawing.Size(133, 25);
            this.toolStripService.TabIndex = 0;
            // 
            // toolButtonStart
            // 
            this.toolButtonStart.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
            this.toolButtonStart.Enabled = false;
            this.toolButtonStart.Image = ((System.Drawing.Image)(resources.GetObject("toolButtonStart.Image")));
            this.toolButtonStart.ImageTransparentColor = System.Drawing.Color.Magenta;
            this.toolButtonStart.Name = "toolButtonStart";
            this.toolButtonStart.Size = new System.Drawing.Size(23, 22);
            this.toolButtonStart.Text = "Dienst starten";
            this.toolButtonStart.Click += new System.EventHandler(this.toolButtonStart_Click);
            // 
            // toolButtonStop
            // 
            this.toolButtonStop.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
            this.toolButtonStop.Enabled = false;
            this.toolButtonStop.Image = ((System.Drawing.Image)(resources.GetObject("toolButtonStop.Image")));
            this.toolButtonStop.ImageTransparentColor = System.Drawing.Color.Magenta;
            this.toolButtonStop.Name = "toolButtonStop";
            this.toolButtonStop.Size = new System.Drawing.Size(23, 22);
            this.toolButtonStop.Text = "Dienst stoppen";
            this.toolButtonStop.Click += new System.EventHandler(this.toolButtonStop_Click);
            // 
            // toolButtonRestart
            // 
            this.toolButtonRestart.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
            this.toolButtonRestart.Enabled = false;
            this.toolButtonRestart.Image = ((System.Drawing.Image)(resources.GetObject("toolButtonRestart.Image")));
            this.toolButtonRestart.ImageTransparentColor = System.Drawing.Color.Magenta;
            this.toolButtonRestart.Name = "toolButtonRestart";
            this.toolButtonRestart.Size = new System.Drawing.Size(23, 22);
            this.toolButtonRestart.Text = "Dienst neustarten";
            this.toolButtonRestart.Click += new System.EventHandler(this.toolButtonRestart_Click);
            // 
            // toolStripSeparator1
            // 
            this.toolStripSeparator1.Name = "toolStripSeparator1";
            this.toolStripSeparator1.Size = new System.Drawing.Size(6, 25);
            // 
            // toolButtonPrefs
            // 
            this.toolButtonPrefs.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
            this.toolButtonPrefs.Image = ((System.Drawing.Image)(resources.GetObject("toolButtonPrefs.Image")));
            this.toolButtonPrefs.ImageTransparentColor = System.Drawing.Color.Magenta;
            this.toolButtonPrefs.Name = "toolButtonPrefs";
            this.toolButtonPrefs.Size = new System.Drawing.Size(23, 22);
            this.toolButtonPrefs.Text = "Einstellungen";
            this.toolButtonPrefs.Click += new System.EventHandler(this.toolButtonPrefs_Click);
            // 
            // toolButtonAbout
            // 
            this.toolButtonAbout.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
            this.toolButtonAbout.Image = ((System.Drawing.Image)(resources.GetObject("toolButtonAbout.Image")));
            this.toolButtonAbout.ImageTransparentColor = System.Drawing.Color.Magenta;
            this.toolButtonAbout.Name = "toolButtonAbout";
            this.toolButtonAbout.Size = new System.Drawing.Size(23, 22);
            this.toolButtonAbout.Text = "Info";
            this.toolButtonAbout.Click += new System.EventHandler(this.toolButtonAbout_Click);
            // 
            // refreshTimer
            // 
            this.refreshTimer.Interval = 2500;
            this.refreshTimer.Tick += new System.EventHandler(this.refreshTimer_Tick);
            // 
            // FormMain
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.BackColor = System.Drawing.SystemColors.Control;
            this.ClientSize = new System.Drawing.Size(612, 479);
            this.Controls.Add(this.toolStripContainer);
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.Name = "FormMain";
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "b1gMailServer-Manager";
            this.Load += new System.EventHandler(this.FormMain_Load);
            this.toolStripContainer.BottomToolStripPanel.ResumeLayout(false);
            this.toolStripContainer.BottomToolStripPanel.PerformLayout();
            this.toolStripContainer.ContentPanel.ResumeLayout(false);
            this.toolStripContainer.TopToolStripPanel.ResumeLayout(false);
            this.toolStripContainer.TopToolStripPanel.PerformLayout();
            this.toolStripContainer.ResumeLayout(false);
            this.toolStripContainer.PerformLayout();
            this.statusStrip.ResumeLayout(false);
            this.statusStrip.PerformLayout();
            this.toolStripService.ResumeLayout(false);
            this.toolStripService.PerformLayout();
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.ToolStripContainer toolStripContainer;
        private System.Windows.Forms.ListView listServices;
        private System.Windows.Forms.ColumnHeader columnService;
        private System.Windows.Forms.ColumnHeader columnStatus;
        private System.Windows.Forms.ColumnHeader columnActive;
        private System.Windows.Forms.ToolStrip toolStripService;
        private System.Windows.Forms.ToolStripButton toolButtonStart;
        private System.Windows.Forms.ToolStripButton toolButtonStop;
        private System.Windows.Forms.ToolStripButton toolButtonRestart;
        private System.Windows.Forms.ToolStripSeparator toolStripSeparator1;
        private System.Windows.Forms.ToolStripButton toolButtonPrefs;
        private System.Windows.Forms.ToolStripButton toolButtonAbout;
        private System.Windows.Forms.Timer refreshTimer;
        private System.Windows.Forms.StatusStrip statusStrip;
        private System.Windows.Forms.ToolStripStatusLabel statusLabel;
        private System.Windows.Forms.ImageList imageList;


    }
}