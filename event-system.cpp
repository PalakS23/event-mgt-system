#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <map>
#include <chrono>
#include <ctime>
#include <cctype>

using namespace std;
// ------------------------------------------------------------
// Smart Event Manager (CLI) — Online-Compiler-Friendly Version
// ------------------------------------------------------------
// Goal: Runs on restricted online IDEs (no file I/O, no external libs).
// What it includes:
// - OOP design (Event + EventManager)
// - Add / Edit / Delete / View / Search
// - Duplicate prevention (name+date+time)
// - Date & time validation (DD-MM-YYYY / HH:MM 24h) — no <regex>
// - Conflict detection (1-hour events) + suggested available slots
// - Day view + Today's events (from system clock)
// - Admin role gating (add/edit/delete/send/statistics)
// - "Event Reminders": paste attendee emails (simulated sending)
//
// NOTE: Persistent storage and .xlsx reading are NOT possible in most
// online IDEs. As a workaround, we provide:
//  - Export Snapshot: print all events as CSV to copy/save manually.
//  - Import Snapshot: paste CSV back to restore events during the run.
// ------------------------------------------------------------

struct Event {
    int id{};                      // auto-increment id
    string name;
    string date;              // DD-MM-YYYY
    string time;              // HH:MM (24h)
    string type;              // e.g. Talk/Workshop/Meeting
    string location;          // optional
};

static string toLower(string s)
{ 
    for(char& c:s) 
    c=tolower((unsigned char)c); 
    return s;
}

static bool iequals(const string& a, const string& b){
    return toLower(a)==toLower(b);
}

static bool icontains(const string& text, const string& key){
    string t=toLower(text), k=toLower(key);
    return t.find(k)!=string::npos;
}

class EventManager {
    vector<Event> events;
    int nextId = 1;
    vector<string> attendeeEmails; // loaded via paste

public:
    // ------------------- Validation -------------------
    static bool isLeap(int y){ return (y%4==0 && y%100!=0) || (y%400==0); }

    static bool isValidDate(const string& d){
        // expect DD-MM-YYYY with digits and '-'
        if (d.size()!=10 || d[2]!='-' || d[5]!='-') return false;
        for (int i=0;i<10;i++){ if (i==2||i==5) continue; if (!isdigit((unsigned char)d[i])) return false; }
        int day = stoi(d.substr(0,2));
        int mon = stoi(d.substr(3,2));
        int yr  = stoi(d.substr(6,4));
        if (yr<1900 || yr>3000) return false;
        if (mon<1 || mon>12) return false;
        int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        if (mon==2 && isLeap(yr)) mdays[2]=29;
        return day>=1 && day<=mdays[mon];
    }

    static bool isValidTime(const string& t){
        // expect HH:MM with digits and ':'
        if (t.size()!=5 || t[2] != ':') return false;
        if (!isdigit((unsigned char)t[0]) || !isdigit((unsigned char)t[1]) ||
            !isdigit((unsigned char)t[3]) || !isdigit((unsigned char)t[4])) return false;
        int h = (t[0]-'0')*10 + (t[1]-'0');
        int m = (t[3]-'0')*10 + (t[4]-'0');
        if (h<0 || h>23) return false;
        if (m<0 || m>59) return false;
        return true;
    }

    static int toMinutes(const string& t){ return (t[0]-'0')*600 + (t[1]-'0')*60 + (t[3]-'0')*10 + (t[4]-'0'); }

    static string fromMinutes(int minutes){
        if (minutes<0) minutes=0; minutes %= (24*60);
        int h = minutes/60, m = minutes%60;
        ostringstream os; os<<setw(2)<<setfill('0')<<h<<":"<<setw(2)<<setfill('0')<<m; return os.str();
    }

    static bool conflicts(const Event& a, const Event& b){
        if (a.date!=b.date) return false;
        int s1=toMinutes(a.time), e1=s1+60; // assume 60-minute events
        int s2=toMinutes(b.time), e2=s2+60;
        return (s1<e2 && s2<e1);
    }

    // ------------------- Utilities -------------------
    static string today(){
        using namespace chrono;
        auto now = system_clock::now();
        time_t tt = system_clock::to_time_t(now);
        tm local{};
        #ifdef _WIN32
            localtime_s(&local, &tt);
        #else
            local = *localtime(&tt);
        #endif
        ostringstream os;
        os<<setw(2)<<setfill('0')<<local.tm_mday<<"-"<<setw(2)<<setfill('0')<<(local.tm_mon+1)<<"-"<<(local.tm_year+1900);
        return os.str();
    }

    static string truncate(const string& s, size_t n){ if(s.size()<=n) return s; return s.substr(0,n-1)+"…"; }

    static void printHeader(){
        cout<<left
                 <<setw(5)<<"ID"
                 <<setw(22)<<"Name"
                 <<setw(12)<<"Date"
                 <<setw(8)<<"Time"
                 <<setw(14)<<"Type"
                 <<setw(18)<<"Location"<<"\n";
        cout<<string(79,'-')<<"\n";
    }

    static void printEvent(const Event& e){
        cout<<left
                 <<setw(5)<<e.id
                 <<setw(22)<<truncate(e.name,20)
                 <<setw(12)<<e.date
                 <<setw(8)<<e.time
                 <<setw(14)<<truncate(e.type,12)
                 <<setw(18)<<truncate(e.location,16)<<"\n";
    }

    // ------------------- Core Ops -------------------
    bool isDuplicate(const string& name, const string& date, const string& time){
        for (const auto& e: events){ if (iequals(e.name,name) && e.date==date && e.time==time) return true; }
        return false;
    }

    bool addEvent(const string& name,const string& date,const string& time,const string& type,const string& location,bool verbose=true){
        if (!isValidDate(date)){ if(verbose) cout<<"Invalid date. Use DD-MM-YYYY.\n"; return false; }
        if (!isValidTime(time)){ if(verbose) cout<<"Invalid time. Use HH:MM (24h).\n"; return false; }
        if (isDuplicate(name,date,time)){ if(verbose) cout<<"Duplicate event exists.\n"; return false; }
        Event e{nextId++,name,date,time,type,location};
        for (const auto& ex: events){ if (conflicts(e,ex)){ if(verbose){ cout<<"Conflict with Event ID "<<ex.id<<" ("<<ex.name<<") at "<<ex.time<<".\n"; suggestSlots(date);} return false; } }
        events.push_back(e);
        if(verbose) cout<<"Event added with ID: "<<e.id<<"\n";
        return true;
    }

    bool editEventById(int id){
        auto it = find_if(events.begin(),events.end(),[&](const Event& e){return e.id==id;});
        if (it==events.end()){ cout<<"Event not found.\n"; return false; }
        Event backup=*it; Event &e=*it; string in;
        cout<<"Editing Event (leave blank to keep current)\n";
        cout<<"Name ["<<e.name<<"]: "; getline(cin,in); if(!in.empty()) e.name=in;
        cout<<"Date ["<<e.date<<"]: "; getline(cin,in); if(!in.empty()) e.date=in;
        cout<<"Time ["<<e.time<<"]: "; getline(cin,in); if(!in.empty()) e.time=in;
        cout<<"Type ["<<e.type<<"]: "; getline(cin,in); if(!in.empty()) e.type=in;
        cout<<"Location ["<<e.location<<"]: "; getline(cin,in); if(!in.empty()) e.location=in;
        if (!isValidDate(e.date) || !isValidTime(e.time)){ cout<<"Invalid date/time. Reverting.\n"; e=backup; return false; }
        for (const auto& ex: events){ if (ex.id!=e.id && iequals(ex.name,e.name) && ex.date==e.date && ex.time==e.time){ cout<<"Duplicate after edit. Reverting.\n"; e=backup; return false; } }
        for (const auto& ex: events){ if (ex.id!=e.id && conflicts(e,ex)){ cout<<"Conflict after edit with ID "<<ex.id<<". Reverting.\n"; suggestSlots(e.date); e=backup; return false; } }
        cout<<"Event updated.\n"; return true;
    }

    bool deleteById(int id){
        auto before = events.size();
        events.erase(remove_if(events.begin(),events.end(),[&](const Event& e){return e.id==id;}),events.end());
        if (events.size()==before){ cout<<"No event with that ID.\n"; return false; }
        cout<<"Deleted.\n"; return true;
    }

    bool deleteByName(const string& name){
        auto before = events.size();
        events.erase(remove_if(events.begin(),events.end(),[&](const Event& e){return iequals(e.name,name);} ),events.end());
        if (events.size()==before){ cout<<"No event with that name.\n"; return false; }
        cout<<"Deleted.\n"; return true;
    }

    void dayView(const string& date){
        vector<Event> list; for (const auto& e: events) if (e.date==date) list.push_back(e);
        sort(list.begin(),list.end(),[](const Event&a,const Event&b){return toMinutes(a.time)<toMinutes(b.time);} );
        if (list.empty()){ cout<<"No events on this date.\n"; return; }
        printHeader(); for (const auto& e: list) printEvent(e);
    }

    void todaysEvents(){ dayView(today()); }

    void listAll(){
        if (events.empty()){ cout<<"No events.\n"; return; }
        vector<Event> list=events;
        auto ymd=[](const string& d){ return d.substr(6,4)+d.substr(3,2)+d.substr(0,2); };
        sort(list.begin(),list.end(),[&](const Event&a,const Event&b){ if (a.date==b.date) return toMinutes(a.time)<toMinutes(b.time); return ymd(a.date)<ymd(b.date);} );
        printHeader(); for (const auto& e: list) printEvent(e);
    }

    void search(const string& keyword){
        vector<Event> list; for (const auto& e: events){ if (icontains(e.name,keyword) || icontains(e.type,keyword)) list.push_back(e); }
        if (list.empty()){ cout<<"No matches.\n"; return; }
        sort(list.begin(),list.end(),[](const Event&a,const Event&b){return a.id<b.id;});
        printHeader(); for (const auto& e: list) printEvent(e);
    }

    void statistics(){
        cout<<"Total events: "<<events.size()<<"\n";
        map<string,int> byType, byDate; for (const auto& e: events){ byType[e.type]++; byDate[e.date]++; }
        cout<<"By type:\n"; for (auto&p: byType) cout<<"  "<<p.first<<": "<<p.second<<"\n";
        vector<pair<string,int>> v(byDate.begin(),byDate.end());
        sort(v.begin(),v.end(),[](auto&a,auto&b){return a.second>b.second;});
        cout<<"Top 5 dates by count:\n"; for(size_t i=0;i<v.size()&&i<5;i++) cout<<"  "<<v[i].first<<": "<<v[i].second<<"\n";
    }

    // ------------------- Reminders (Simulated) -------------------
    void loadAttendeesFromPaste(){
        cout<<"Paste emails (comma/space/newline separated). End with a blank line.\n";
        attendeeEmails.clear();
        string line, all;
        while (true){
            getline(cin,line);
            if (line.size()==0) break; all += line + ' ';
        }
        stringstream ss(all); string token;
        auto isEmail=[&](const string&s){ return s.find('@')!=string::npos && s.find('.')!=string::npos; };
        while (ss>>token){ if (token.back()==','||token.back()==';') token.pop_back(); if (isEmail(token)) attendeeEmails.push_back(token); }
        cout<<"Loaded "<<attendeeEmails.size()<<" attendee emails.\n";
    }

    void sendReminderForDate(const string& date){
        vector<Event> list; for (const auto& e: events) if (e.date==date) list.push_back(e);
        if (list.empty()){ cout<<"No events on this date.\n"; return; }
        sort(list.begin(),list.end(),[](const Event&a,const Event&b){return toMinutes(a.time)<toMinutes(b.time);} );
        ostringstream body; body<<"Upcoming events on "<<date<<":\n\n";
        for (const auto& e: list) body<<"- "<<e.time<<" | "<<e.name<<" ("<<e.type<<") @ "<<(e.location.empty()?"TBA":e.location)<<"\n";
        if (attendeeEmails.empty()){
            cout<<"No attendee emails loaded. Choose 'Load attendees' first.\n"; return;
        }
        cout<<"[SIMULATED EMAIL SEND] To "<<attendeeEmails.size()<<" recipients.\nSubject: Reminder: Events on "<<date<<"\n\n"<<body.str();
        cout<<"(Emails not actually sent in online IDE.)\n";
    }

    // ------------------- Suggestions -------------------
    void suggestSlots(const string& date, int duration=60){
        cout<<"Suggested available slots on "<<date<<":\n";
        vector<pair<int,int>> occ; for (const auto& e: events) if (e.date==date){ int s=toMinutes(e.time); occ.push_back({s,s+60}); }
        sort(occ.begin(),occ.end());
        int start=8*60, end=20*60, shown=0;
        for (int t=start; t+duration<=end && shown<5; t+=30){ bool clash=false; for (auto& iv: occ){ if (!(t+duration<=iv.first || t>=iv.second)) { clash=true; break; } } if(!clash){ cout<<"  - "<<fromMinutes(t)<<" to "<<fromMinutes(t+duration)<<"\n"; shown++; } }
        if (!shown) cout<<"  (No free 1-hour slots found in working window)\n";
    }

    // ------------------- Snapshot (manual persistence aid) -------------------
    void exportSnapshotCSV(){
        cout<<"id,name,date,time,type,location\n";
        for (const auto& e: events){
            cout<<e.id<<","<<e.name<<","<<e.date<<","<<e.time<<","<<e.type<<","<<e.location<<"\n";
        }
        cout<<"(Copy the above lines to save. Import with the menu option.)\n";
    }

    void importSnapshotCSV(){
        cout<<"Paste CSV lines (header optional). End with a blank line.\n";
        string line; vector<Event> temp; int maxId=0; bool first=true;
        while (true){
            getline(cin,line); if (line.size()==0) break; if (line.find(",")==string::npos) continue;
            if (first && toLower(line).find("id,name,date,time,type,location")!=string::npos){ first=false; continue; }
            first=false;
            stringstream ss(line); string tok; Event e; int col=0;
            while (getline(ss,tok,',')){
                switch(col){
                    case 0: if (!tok.empty()) e.id = stoi(tok); break;
                    case 1: e.name = tok; break;
                    case 2: e.date = tok; break;
                    case 3: e.time = tok; break;
                    case 4: e.type = tok; break;
                    case 5: e.location = tok; break;
                }
                col++;
            }
            if (e.id==0 || e.name.empty() || !isValidDate(e.date) || !isValidTime(e.time)) continue;
            temp.push_back(e); maxId=max(maxId,e.id);
        }
        if (temp.empty()){ cout<<"Nothing imported.\n"; return; }
        events = temp; nextId = maxId+1; cout<<"Imported "<<events.size()<<" events. Next ID: "<<nextId<<"\n";
    }
};

// ------------------- CLI -------------------

static bool isAdmin = false;

void adminLogin(){
    string user, pass; cout<<"\n== Admin Login ==\nUsername: "; getline(cin,user); cout<<"Password: "; getline(cin,pass);
    if ((user=="admin" || user=="ACMadmin") && pass=="admin123") { isAdmin=true; cout<<"Logged in as admin.\n"; }
    else cout<<"Invalid credentials. Continuing as viewer.\n";
}

void menu(){
    cout<<"\n====== Smart Event Manager ======\n";
    cout<<"1) List all events\n";
    cout<<"2) Day view (pick date)\n";
    cout<<"3) Today's events\n";
    cout<<"4) Search events\n";
    if (isAdmin){
        cout<<"5) Add event (admin)\n";
        cout<<"6) Edit event by ID (admin)\n";
        cout<<"7) Delete event by ID (admin)\n";
        cout<<"8) Delete event by name (admin)\n";
        cout<<"9) Load attendees (paste emails) (admin)\n";
        cout<<"10) Send reminders (admin)\n";
        cout<<"11) Statistics (admin)\n";
        cout<<"12) Export snapshot CSV (admin)\n";
        cout<<"13) Import snapshot CSV (admin)\n";
    }
    cout<<"0) Exit\nSelect: ";
}

int main(){
    EventManager mgr;

    cout<<"Login as admin? (y/N): "; string ans; getline(cin,ans); if (!ans.empty() && (ans=="y"||ans=="Y")) adminLogin();

    while (true){
        menu(); string choice; getline(cin,choice); if (choice=="0"||cin.eof()) break;
        if (choice=="1"){
            mgr.listAll();
        } else if (choice=="2"){
            string d; cout<<"Enter date (DD-MM-YYYY): "; getline(cin,d);
            if (!EventManager::isValidDate(d)){ cout<<"Invalid date.\n"; continue; }
            mgr.dayView(d);
        } else if (choice=="3"){
            mgr.todaysEvents();
        } else if (choice=="4"){
            string k; cout<<"Keyword (name/type): "; getline(cin,k); mgr.search(k);
        } else if (isAdmin && choice=="5"){
            string name,date,time,type,loc; cout<<"Name: "; getline(cin,name);
            cout<<"Date (DD-MM-YYYY): "; getline(cin,date);
            cout<<"Time (HH:MM 24h): "; getline(cin,time);
            cout<<"Type: "; getline(cin,type);
            cout<<"Location (optional): "; getline(cin,loc);
            mgr.addEvent(name,date,time,type,loc);
        } else if (isAdmin && choice=="6"){
            string s; cout<<"ID to edit: "; getline(cin,s);
            if (s.empty() || any_of(s.begin(),s.end(),[](char c){return !isdigit((unsigned char)c);})){ cout<<"Invalid ID.\n"; continue; }
            mgr.editEventById(stoi(s));
        } else if (isAdmin && choice=="7"){
            string s; cout<<"ID to delete: "; getline(cin,s);
            if (s.empty() || any_of(s.begin(),s.end(),[](char c){return !isdigit((unsigned char)c);})){ cout<<"Invalid ID.\n"; continue; }
            mgr.deleteById(stoi(s));
        } else if (isAdmin && choice=="8"){
            string n; cout<<"Name to delete: "; getline(cin,n); mgr.deleteByName(n);
        } else if (isAdmin && choice=="9"){
            mgr.loadAttendeesFromPaste();
        } else if (isAdmin && choice=="10"){
            string d; cout<<"Send reminders for date (DD-MM-YYYY): "; getline(cin,d);
            if (!EventManager::isValidDate(d)){ cout<<"Invalid date.\n"; continue; }
            mgr.sendReminderForDate(d);
        } else if (isAdmin && choice=="11"){
            mgr.statistics();
        } else if (isAdmin && choice=="12"){
            mgr.exportSnapshotCSV();
        } else if (isAdmin && choice=="13"){
            mgr.importSnapshotCSV();
        } else {
            cout<<"Invalid choice."<<(isAdmin?" Try 0-13.":" Try 0-4.")<<"\n";
        }
    }

    cout<<"Goodbye!\n";
    return 0;
}