using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using MetroFramework.Forms;

using System.IO;
using System.Text.RegularExpressions;
using System.Diagnostics;


namespace miniprj_pi_gui
{
    public partial class Form1 : MetroForm
    {
        private int pid_camera;
        private int pid_screen;
        private int pid_upload_doc;
        private int pid_upload_meta;
        private int pid_upload_both;

        private const string upload_doc_security = "1_Upload_Document.py";
        private const string upload_meta_security = "2_Upload_Metadata.py";
        private const string upload_both_security = "3_Upload_Security.py";


        public Form1()
        {
            InitializeComponent();
            SetInitVariables();
        }

        private void SetInitVariables()
        {
            //카메라 필터링 기본값 설정
            TextBox_cam_tessdata.Text = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-masking-pi\\external\\share\\tessdata";
            TextBox_cam_number.Text = "0";
            TextBox_cam_filter.Text = "3";

            //화면공유 필터링 기본값 설정
            TextBox_screen_tessdata.Text = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-masking-pi\\external\\share\\tessdata";

            //업로드 보안 기본값 설정
            TextBox_upload_conda.Text = "C:\\Users\\" + Environment.UserName + "\\anaconda3";
            TextBox_upload_vname.Text = "uploadtest";

            //데이터베이스 경로, 프레임 버퍼 기본값 설정
            TextBox_conf_db.Text = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\privacy_discrimination.db";
            TextBox_conf_buffer.Text = "256";

            UpdateCameraList();
        }

        private void UpdateCameraList()
        {
            StringBuilder outputBuilder = new StringBuilder();

            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.RedirectStandardOutput = true;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-masking-pi\\x64\\Release\\masking-camera.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = "." + " ";

            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            exeProcess.OutputDataReceived += new DataReceivedEventHandler(
                delegate (object _sender, DataReceivedEventArgs _)
                {
                    outputBuilder.Append(_.Data);
                });

            try
            {
                exeProcess.Start();
                exeProcess.BeginOutputReadLine();
                exeProcess.WaitForExit();
                exeProcess.CancelOutputRead();
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
            string camera_available_list = outputBuilder.ToString();
            camera_available_list = camera_available_list.Replace('@', '\r');
            camera_available_list = camera_available_list.Replace('#', '\n');
            TextBox_cam_list.Text = Regex.Unescape(camera_available_list);
        }

        private void TextBox_cam_number_KeyPress(object sender, KeyPressEventArgs e)
        {
            if (!char.IsControl(e.KeyChar) && !char.IsDigit(e.KeyChar) && (e.KeyChar != '.'))
                e.Handled = true;
            if ((e.KeyChar == '.') && ((sender as TextBox).Text.IndexOf('.') > -1))
                e.Handled = true;
        }

        private void TextBox_cam_filter_KeyPress(object sender, KeyPressEventArgs e)
        {
            if (!char.IsControl(e.KeyChar) && !char.IsDigit(e.KeyChar) && (e.KeyChar != '.'))
                e.Handled = true;
            if ((e.KeyChar == '.') && ((sender as TextBox).Text.IndexOf('.') > -1))
                e.Handled = true;
        }

        private void button_camera_start_Click(object sender, EventArgs e)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-masking-pi\\x64\\Release\\masking-camera.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = TextBox_cam_tessdata.Text + " " + TextBox_cam_number.Text + " " + TextBox_cam_filter.Text + " " + TextBox_conf_buffer.Text + " " + TextBox_conf_db.Text;
            
            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            try
            {
                exeProcess.Start();
                pid_camera = exeProcess.Id;
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
        }

        private void button_camera_stop_Click(object sender, EventArgs e)
        {
            Process[] process = Process.GetProcesses();
            foreach (Process proc in process)
            {
                if (proc.Id == pid_camera)
                {
                    proc.Kill();
                    break;
                }
            }
        }

        private void button_screen_start_Click(object sender, EventArgs e)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-masking-pi\\x64\\Release\\masking-screenshare.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = TextBox_screen_tessdata.Text + " " + TextBox_conf_buffer.Text + " " + TextBox_conf_db.Text;

            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            try
            {
                exeProcess.Start();
                pid_screen = exeProcess.Id;
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
        }

        private void button_screen_stop_Click(object sender, EventArgs e)
        {
            Process[] process = Process.GetProcesses();
            foreach (Process proc in process)
            {
                if (proc.Id == pid_screen)
                {
                    proc.Kill();
                    break;
                }
            }
        }

        private void button_upload_doc_start_Click(object sender, EventArgs e)
        {
            var exeProcess = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    RedirectStandardInput = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    WorkingDirectory = TextBox_upload_conda.Text + "\\Scripts",
                    CreateNoWindow = true
                }
            };
            exeProcess.Start();
            pid_upload_doc = exeProcess.Id;

            using (var sw = exeProcess.StandardInput)
            {
                if (sw.BaseStream.CanWrite)
                {
                    sw.WriteLine(TextBox_upload_conda.Text + "\\Scripts\\activate.bat");
                    sw.WriteLine("activate " + TextBox_upload_vname.Text);
                    sw.WriteLine("cd c:\\python");
                    sw.WriteLine("python " + upload_doc_security);
                }
            }
        }

        private void button_upload_doc_stop_Click(object sender, EventArgs e)
        {
            Process[] process = Process.GetProcesses();
            foreach (Process proc in process)
            {
                if (proc.Id == pid_upload_doc)
                {
                    proc.Kill();
                    break;
                }
            }
        }

        private void button_upload_meta_start_Click(object sender, EventArgs e)
        {
            var exeProcess = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    RedirectStandardInput = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    WorkingDirectory = TextBox_upload_conda.Text + "\\Scripts",
                    CreateNoWindow = true
                }
            };
            exeProcess.Start();
            pid_upload_meta = exeProcess.Id;

            using (var sw = exeProcess.StandardInput)
            {
                if (sw.BaseStream.CanWrite)
                {
                    sw.WriteLine(TextBox_upload_conda.Text + "\\Scripts\\activate.bat");
                    sw.WriteLine("activate " + TextBox_upload_vname.Text);
                    sw.WriteLine("cd c:\\python");
                    sw.WriteLine("python " + upload_meta_security);
                }
            }
        }

        private void button_upload_meta_stop_Click(object sender, EventArgs e)
        {
            Process[] process = Process.GetProcesses();
            foreach (Process proc in process)
            {
                if (proc.Id == pid_upload_meta)
                {
                    proc.Kill();
                    break;
                }
            }
        }

        private void button_upload_both_start_Click(object sender, EventArgs e)
        {
            var exeProcess = new Process
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    RedirectStandardInput = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    WorkingDirectory = TextBox_upload_conda.Text + "\\Scripts",
                    CreateNoWindow = true
                }
            };
            exeProcess.Start();
            pid_upload_both = exeProcess.Id;

            using (var sw = exeProcess.StandardInput)
            {
                if (sw.BaseStream.CanWrite)
                {
                    sw.WriteLine(TextBox_upload_conda.Text + "\\Scripts\\activate.bat");
                    sw.WriteLine("activate " + TextBox_upload_vname.Text);
                    sw.WriteLine("cd c:\\python");
                    sw.WriteLine("python " + upload_both_security);
                }
            }
        }

        private void button_upload_both_stop_Click(object sender, EventArgs e)
        {
            Process[] process = Process.GetProcesses();
            foreach (Process proc in process)
            {
                if (proc.Id == pid_upload_both)
                {
                    proc.Kill();
                    break;
                }
            }
        }

        private void button_conf_add_inc_Click(object sender, EventArgs e)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-dbmanager\\x64\\Release\\miniprj-dbmanager.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = TextBox_conf_db.Text + " insert userdefined_include " + TextBox_conf_add_inc.Text;

            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            try
            {
                exeProcess.Start();
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
        }

        private void buttonconf_del_inc_Click(object sender, EventArgs e)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-dbmanager\\x64\\Release\\miniprj-dbmanager.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = TextBox_conf_db.Text + " delete userdefined_include " + TextBox_conf_del_inc.Text;

            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            try
            {
                exeProcess.Start();
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
        }

        private void buttonconf_add_exc_Click(object sender, EventArgs e)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-dbmanager\\x64\\Release\\miniprj-dbmanager.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = TextBox_conf_db.Text + " insert userdefined_exclude " + TextBox_conf_add_exc.Text;

            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            try
            {
                exeProcess.Start();
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
        }

        private void buttonconf_del_exc_Click(object sender, EventArgs e)
        {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            startInfo.FileName = "C:\\Users\\" + Environment.UserName + "\\source\\repos\\miniprj-dbmanager\\x64\\Release\\miniprj-dbmanager.exe";
            startInfo.WindowStyle = ProcessWindowStyle.Hidden;
            startInfo.Arguments = TextBox_conf_db.Text + " delete userdefined_exclude " + TextBox_conf_del_exc.Text;

            Process exeProcess = new Process();
            exeProcess.StartInfo = startInfo;
            try
            {
                exeProcess.Start();
            }
            catch (System.ComponentModel.Win32Exception err)
            {
                MessageBox.Show(err.Message);
            }
        }
    }
}
