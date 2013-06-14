#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/splitter.h>
#include <wx/settings.h>
#include <wx/grid.h>
#include <wx/numdlg.h>
#include <limits.h>

#include "querypanel.h"
#include "kveditordialog.h"
#include "selectdbdialog.h"
#include "simplechart.h"

QueryPanel::QueryPanel(wxWindow *parent, RedisConnection *connection):
    wxPanel(parent), m_connection(connection), m_currentDb(0)
{
    m_rawCommandFont = new wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("Courier 10 Pitch"));

    wxNotebook *expTabs = new wxNotebook(this, ID_TAB_EXPLORE);

    // dashboard
    wxPanel *dashboardPanel = new wxPanel(expTabs);
    wxBoxSizer *vboxDashboard = new wxBoxSizer(wxVERTICAL);
    TimeLogChart *memChart = new TimeLogChart(dashboardPanel, ID_MEMORY_CHART, wxT("Memory Status"));
    memChart->SetValueAxisFormat("%.1fM");
    memChart->AddChart("Used", wxColour(*wxBLUE));
    memChart->AddChart("Peak Used", *wxRED);
    memChart->AddChart("RSS", wxColour("orange"));

    vboxDashboard->Add(memChart, 1, wxEXPAND | wxTOP | wxBOTTOM, 5);

    TimeLogChart *cpuChart = new TimeLogChart(dashboardPanel, ID_CPU_CHART, wxT("CPU Status"));
    cpuChart->SetValueAxisFormat("%.1f");
    cpuChart->SetValueAxisScale(1.0);
    cpuChart->AddChart("Used", wxColour(*wxGREEN));
    cpuChart->AddChart("Peak Used", *wxRED);

    vboxDashboard->Add(cpuChart, 1, wxEXPAND | wxTOP | wxBOTTOM, 5);
    vboxDashboard->Add(-1, -1, 1);
    dashboardPanel->SetSizer(vboxDashboard);
    expTabs->AddPage(dashboardPanel, wxT("Server Status"));

    // keys searching
    wxPanel *keysSearchingPanel = new wxPanel(expTabs);
    wxTextCtrl *txtKeysSearch = new wxTextCtrl(keysSearchingPanel, ID_TEXT_KEY, "", wxDefaultPosition, wxSize(-1, 28), wxTE_PROCESS_ENTER, wxDefaultValidator);
    wxButton *btnKeySearch = new wxButton(keysSearchingPanel, ID_COMMAND_FIND_KEYS, wxT("Find"), wxPoint(-1,-1), wxSize(-1, 28));
    wxListBox *lboxKeys = new wxListBox(keysSearchingPanel, ID_LBOX_KEYS, wxDefaultPosition, wxDefaultSize);

    wxBoxSizer *hboxKeysSearch = new wxBoxSizer(wxHORIZONTAL);
    hboxKeysSearch->Add(txtKeysSearch, 1, wxEXPAND);
    hboxKeysSearch->Add(btnKeySearch, 0);

    wxBoxSizer *vboxKeysSearch = new wxBoxSizer(wxVERTICAL);
    vboxKeysSearch->Add(hboxKeysSearch, 0, wxALIGN_LEFT | wxEXPAND | wxALL, 3);
    vboxKeysSearch->Add(lboxKeys, 1, wxEXPAND | wxALL & ~wxTOP, 3);

    keysSearchingPanel->SetSizer(vboxKeysSearch);
    expTabs->AddPage(keysSearchingPanel, wxT("Find Keys"));

    // raw commands
    wxPanel *rawCommandPanel = new wxPanel(expTabs);
    wxTextCtrl *txtCommand = new wxTextCtrl(rawCommandPanel, ID_TEXT_COMMAND, wxEmptyString, wxDefaultPosition, wxSize(-1, 28), wxTE_PROCESS_ENTER, wxDefaultValidator);
    wxButton *btnCommand = new wxButton(rawCommandPanel, ID_COMMAND_RAW, wxT("Execute"), wxPoint(-1,-1), wxSize(-1, 28));
    wxTextCtrl *txtCommandResult = new wxTextCtrl(rawCommandPanel, ID_TEXT_COMMAND_RESULT, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    txtCommandResult->SetBackgroundColour(*wxBLACK);
    txtCommandResult->SetForegroundColour(*wxLIGHT_GREY);
    txtCommandResult->SetFont(*m_rawCommandFont);
    txtCommandResult->SetEditable(false);

    wxBoxSizer *hboxCommand = new wxBoxSizer(wxHORIZONTAL);
    hboxCommand->Add(txtCommand, 1, wxEXPAND);
    hboxCommand->Add(btnCommand, 0);

    wxBoxSizer *vboxCommand = new wxBoxSizer(wxVERTICAL);
    vboxCommand->Add(hboxCommand, 0, wxALIGN_LEFT | wxEXPAND | wxALL, 3);
    vboxCommand->Add(txtCommandResult, 1, wxEXPAND | wxALL & ~wxTOP, 3);

    rawCommandPanel->SetSizer(vboxCommand);
    expTabs->AddPage(rawCommandPanel, wxT("Raw Commands"));

    wxBoxSizer *hboxMain = new wxBoxSizer(wxHORIZONTAL);
    hboxMain->Add(expTabs, 1, wxEXPAND | wxALL, 3);

    SetSizer(hboxMain);

    m_timer = new wxTimer(this, ID_TIMER);
    m_timer->Start(1000);

    Connect(ID_TIMER, wxEVT_TIMER, wxTimerEventHandler(QueryPanel::OnTimer));
    Connect(ID_COMMAND_FIND_KEYS, wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(QueryPanel::OnFind));
    Connect(ID_TEXT_KEY, wxEVT_COMMAND_TEXT_ENTER, wxKeyEventHandler(QueryPanel::OnEnterKey));
    Connect(ID_LBOX_KEYS, wxEVT_COMMAND_LISTBOX_SELECTED, wxCommandEventHandler(QueryPanel::OnKeySelected));
    Connect(ID_LBOX_KEYS, wxEVT_COMMAND_LISTBOX_DOUBLECLICKED, wxCommandEventHandler(QueryPanel::OnKeyDoubleClicked));
    Connect(ID_COMMAND_RAW, wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(QueryPanel::OnRawCommand));
    Connect(ID_TEXT_COMMAND, wxEVT_COMMAND_TEXT_ENTER, wxKeyEventHandler(QueryPanel::OnEnterKey));
}

QueryPanel::~QueryPanel()
{
    if (m_connection != NULL)
    {
        delete m_connection;
        m_connection = NULL;
    }

    delete m_rawCommandFont;
}

void QueryPanel::OnTimer(wxTimerEvent &evt)
{
    if (m_connection != NULL && m_connection->IsConnected())
    {
        RedisSystemStatus sysStats = m_connection->GetMemoryStatus();

        TimeLogChart *memChart = (TimeLogChart *)FindWindow(ID_MEMORY_CHART);
        if (memChart != NULL)
        {
            memChart->AddChartValue(0, (double)sysStats.GetUsed());
            memChart->AddChartValue(1, (double)sysStats.GetPeak());
            memChart->AddChartValue(2, (double)sysStats.GetRss());
        }

        TimeLogChart *cpuChart = (TimeLogChart *)FindWindow(ID_CPU_CHART);
        if (cpuChart != NULL)
        {
            cpuChart->AddChartValue(0, sysStats.GetCPUSys());
            cpuChart->AddChartValue(1, sysStats.GetCPUUser());
        }
    }

//    wxSafeShowMessage("TEST", wxString::Format("%d", rand()));
}

wxString QueryPanel::GetSearchText() const
{
    return ((wxTextCtrl*)FindWindow(ID_TEXT_KEY))->GetValue();
}

wxString QueryPanel::GetCommandText() const
{
    return ((wxTextCtrl*)FindWindow(ID_TEXT_COMMAND))->GetValue();
}

void QueryPanel::OnKeyDoubleClicked(wxCommandEvent &evt)
{
    EditKeyValue();
}

wxListBox *QueryPanel::GetKeyListBox() const
{
    wxWindow *ctrl = FindWindow(ID_LBOX_KEYS);
    if (ctrl != NULL)
    {
        return (wxListBox*)ctrl;
    }

    return NULL;
}

void QueryPanel::OnKeySelected(wxCommandEvent &evt)
{
    if (m_connection != NULL && m_connection->IsConnected())
    {
    }
}

wxString QueryPanel::GetSelectedKey() const
{
    if (GetKeyListBox() != NULL)
    {
        return GetKeyListBox()->GetStringSelection();
    }

    return "";
}

void QueryPanel::OnFind(wxCommandEvent& evt)
{
    FindKeys(GetSearchText());
}

void QueryPanel::OnRawCommand(wxCommandEvent &evt)
{
    ExecuteCommand(GetCommandText());
}

void QueryPanel::OnEnterKey(wxKeyEvent& evt)
{
    if (evt.GetEventObject() == FindWindow(ID_TEXT_KEY))
    {
        FindKeys(GetSearchText());
    }
    else if (evt.GetEventObject() == FindWindow(ID_TEXT_COMMAND))
    {
        ExecuteCommand(GetCommandText());
    }
}

void QueryPanel::OnDisconnect(wxCommandEvent& evt)
{
    CloseConnection();
}

void QueryPanel::ExecuteCommand(const wxString &command)
{
    if (!command.IsEmpty() && m_connection != NULL && m_connection->IsConnected())
    {
        RedisSimpleValue ret = m_connection->ExecuteCommand(command);
        if (ret.GetValueType() != REDIS_NONE_VALUE)
        {
            wxTextCtrl *result = (wxTextCtrl*)FindWindow(ID_TEXT_COMMAND_RESULT);
            if (ret.GetValueType() == REDIS_INT_VALUE)
                result->SetValue(wxString::Format("%d", ret.GetIntValue()));
            else
                result->SetValue(ret.GetStrValue());
        }
    }
}

void QueryPanel::FindKeys(const wxString& keyFilter)
{
    GetKeyListBox()->Clear();
    if (m_connection)
    {
        m_connection->FindKeys(keyFilter);
        wxArrayString &result = m_connection->GetKeysResult();
        for (int i = 0; i < result.Count(); i++)
        {
            GetKeyListBox()->Append(result[i]);
        }
    }
}

void QueryPanel::CloseConnection()
{
    if (m_timer != NULL)
    {
        m_timer->Stop();
        delete m_timer;
        m_timer = NULL;
    }

    wxNotebook *nb = ((wxNotebook*)GetParent());
    for (int i = 0; i < nb->GetPageCount(); i++)
    {
        if (nb->GetPage(i) == this) {
            nb->DeletePage(i);
            break;
        }
    }
}

void QueryPanel::EditKeyValue()
{
    wxString key = GetSelectedKey();
    if (key != "" && m_connection != NULL && m_connection->IsConnected())
    {
        KeyValueEditorDialog dlg(this, wxT("Edit Key-Value Pair"), key, m_connection->GetValue(key));
        if (dlg.ShowModal() == wxID_OK)
        {
            m_connection->SetValue(dlg.GetKey(), dlg.GetValue());
        }
    }
}

void QueryPanel::AddKeyValue()
{
    KeyValueEditorDialog dlg(this, wxT("Add Key-Value Pair"), "", RedisSimpleValue(""));
    if (dlg.ShowModal() == wxID_OK && m_connection != NULL && m_connection->IsConnected())
    {
        m_connection->SetValue(dlg.GetKey(), dlg.GetValue());
    }
}

void QueryPanel::DeleteKey()
{
    wxMessageDialog dlg(this, wxT("Are you sure to delete key: '" + GetSelectedKey() + "'' ?"), wxT("Delete Key"), wxNO_DEFAULT | wxYES_NO | wxCANCEL | wxICON_ASTERISK);
    if (dlg.ShowModal() == wxID_YES && m_connection != NULL && m_connection->IsConnected())
    {
        if (m_connection->DeleteKey(GetSelectedKey()))
        {
            int selectedIndex = GetKeyListBox()->GetSelection();
            if (selectedIndex != -1)
            {
                GetKeyListBox()->Delete((unsigned int)selectedIndex);
            }
        }
    }
}

void QueryPanel::SelectDb()
{
    SelectDbdialog dlg(this, wxT("Select Database"), m_currentDb);
    if (dlg.ShowModal() == wxID_OK && m_connection != NULL && m_connection->IsConnected())
    {
        uint selectedDb = dlg.GetDb();
        if (m_connection->SelectDb(selectedDb))
        {
            m_currentDb = selectedDb;
            FindKeys(GetSearchText());
        }
    }
}

void QueryPanel::ExpireKey()
{
    wxNumberEntryDialog dlg(this, wxT(""), wxT("Please set the expiration time in seconds"), wxT("Set Key Expiration"), 10, 1, 2147483647);
    if (dlg.ShowModal() == wxID_OK && m_connection != NULL && m_connection->IsConnected())
    {
        m_connection->Expire(GetSelectedKey(), (int)dlg.GetValue());
    }
}

wxString QueryPanel::GetServerInfo() const
{
    if (m_connection != NULL && m_connection->IsConnected())
        return m_connection->GetServerInfo();

    return wxEmptyString;
}

