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
    std::string name;
    std::string date;              // DD-MM-YYYY
    std::string time;              // HH:MM (24h)
    std::string type;              // e.g. Talk/Workshop/Meeting
    std::string location;          // optional
};

static std::string toLower(std::string s){ for(char& c:s) c=std::tolower((unsigned char)c); return s; }

static bool iequals(const std::string& a, const std::string& b){
    return toLower(a)==toLower(b);
}

static bool icontains(const std::string& text, const std::string& key){
    std::string t=toLower(text), k=toLower(key);
    return t.find(k)!=std::string::npos;
}

class EventManager {
    std::vector<Event> events;
    int nextId = 1;
    std::vector<std::string> attendeeEmails; // loaded via paste

public:
    // ------------------- Validation -------------------
    static bool isLeap(int y){ return (y%4==0 && y%100!=0) || (y%400==0); }

    static bool isValidDate(const std::string& d){
        // expect DD-MM-YYYY with digits and '-'
        if (d.size()!=10 || d[2]!='-' || d[5]!='-') return false;
        for (int i=0;i<10;i++){ if (i==2||i==5) continue; if (!std::isdigit((unsigned char)d[i])) return false; }
        int day = std::stoi(d.substr(0,2));
        int mon = std::stoi(d.substr(3,2));
        int yr  = std::stoi(d.substr(6,4));
        if (yr<1900 || yr>3000) return false;
        if (mon<1 || mon>12) return false;
        int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
        if (mon==2 && isLeap(yr)) mdays[2]=29;
        return day>=1 && day<=mdays[mon];
    }

    static bool isValidTime(const std::string& t){
        // expect HH:MM with digits and ':'
        if (t.size()!=5 || t[2] != ':') return false;
        if (!std::isdigit((unsigned char)t[0]) || !std::isdigit((unsigned char)t[1]) ||
            !std::isdigit((unsigned char)t[3]) || !std::isdigit((unsigned char)t[4])) return false;
        int h = (t[0]-'0')*10 + (t[1]-'0');
        int m = (t[3]-'0')*10 + (t[4]-'0');
        if (h<0 || h>23) return false;
        if (m<0 || m>59) return false;
        return true;
    }

    static int toMinutes(const std::string& t){ return (t[0]-'0')*600 + (t[1]-'0')*60 + (t[3]-'0')*10 + (t[4]-'0'); }

    static std::string fromMinutes(int minutes){
        if (minutes<0) minutes=0; minutes %= (24*60);
        int h = minutes/60, m = minutes%60;
        std::ostringstream os; os<<std::setw(2)<<std::setfill('0')<<h<<":"<<std::setw(2)<<std::setfill('0')<<m; return os.str();
    }

    static bool conflicts(const Event& a, const Event& b){
        if (a.date!=b.date) return false;
        int s1=toMinutes(a.time), e1=s1+60; // assume 60-minute events
        int s2=toMinutes(b.time), e2=s2+60;
        return (s1<e2 && s2<e1);
    }

    // ------------------- Utilities -------------------
    static std::string today(){
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t tt = system_clock::to_time_t(now);
        std::tm local{};
        #ifdef _WIN32
            localtime_s(&local, &tt);
        #else
            local = *std::localtime(&tt);
        #endif
        std::ostringstream os;
        os<<std::setw(2)<<std::setfill('0')<<local.tm_mday<<"-"<<std::setw(2)<<std::setfill('0')<<(local.tm_mon+1)<<"-"<<(local.tm_year+1900);
        return os.str();
    }

    static std::string truncate(const std::string& s, size_t n){ if(s.size()<=n) return s; return s.substr(0,n-1)+"…"; }

    static void printHeader(){
        std::cout<<std::left
                 <<std::setw(5)<<"ID"
                 <<std::setw(22)<<"Name"
                 <<std::setw(12)<<"Date"
                 <<std::setw(8)<<"Time"
                 <<std::setw(14)<<"Type"
                 <<std::setw(18)<<"Location"<<"\n";
        std::cout<<std::string(79,'-')<<"\n";
    }

    static void printEvent(const Event& e){
        std::cout<<std::left
                 <<std::setw(5)<<e.id
                 <<std::setw(22)<<truncate(e.name,20)
                 <<std::setw(12)<<e.date
                 <<std::setw(8)<<e.time
                 <<std::setw(14)<<truncate(e.type,12)
                 <<std::setw(18)<<truncate(e.location,16)<<"\n";
    }

    // ------------------- Core Ops -------------------
    bool isDuplicate(const std::string& name, const std::string& date, const std::string& time){
        for (const auto& e: events){ if (iequals(e.name,name) && e.date==date && e.time==time) return true; }
        return false;
    }

    bool addEvent(const std::string& name,const std::string& date,const std::string& time,const std::string& type,const std::string& location,bool verbose=true){
        if (!isValidDate(date)){ if(verbose) std::cout<<"Invalid date. Use DD-MM-YYYY.\n"; return false; }
        if (!isValidTime(time)){ if(verbose) std::cout<<"Invalid time. Use HH:MM (24h).\n"; return false; }
        if (isDuplicate(name,date,time)){ if(verbose) std::cout<<"Duplicate event exists.\n"; return false; }
        Event e{nextId++,name,date,time,type,location};
        for (const auto& ex: events){ if (conflicts(e,ex)){ if(verbose){ std::cout<<"Conflict with Event ID "<<ex.id<<" ("<<ex.name<<") at "<<ex.time<<".\n"; suggestSlots(date);} return false; } }
        events.push_back(e);
        if(verbose) std::cout<<"Event added with ID: "<<e.id<<"\n";
        return true;
    }

    bool editEventById(int id){
        auto it = std::find_if(events.begin(),events.end(),[&](const Event& e){return e.id==id;});
        if (it==events.end()){ std::cout<<"Event not found.\n"; return false; }
        Event backup=*it; Event &e=*it; std::string in;
        std::cout<<"Editing Event (leave blank to keep current)\n";
        std::cout<<"Name ["<<e.name<<"]: "; std::getline(std::cin,in); if(!in.empty()) e.name=in;
        std::cout<<"Date ["<<e.date<<"]: "; std::getline(std::cin,in); if(!in.empty()) e.date=in;
        std::cout<<"Time ["<<e.time<<"]: "; std::getline(std::cin,in); if(!in.empty()) e.time=in;
        std::cout<<"Type ["<<e.type<<"]: "; std::getline(std::cin,in); if(!in.empty()) e.type=in;
        std::cout<<"Location ["<<e.location<<"]: "; std::getline(std::cin,in); if(!in.empty()) e.location=in;
        if (!isValidDate(e.date) || !isValidTime(e.time)){ std::cout<<"Invalid date/time. Reverting.\n"; e=backup; return false; }
        for (const auto& ex: events){ if (ex.id!=e.id && iequals(ex.name,e.name) && ex.date==e.date && ex.time==e.time){ std::cout<<"Duplicate after edit. Reverting.\n"; e=backup; return false; } }
        for (const auto& ex: events){ if (ex.id!=e.id && conflicts(e,ex)){ std::cout<<"Conflict after edit with ID "<<ex.id<<". Reverting.\n"; suggestSlots(e.date); e=backup; return false; } }
        std::cout<<"Event updated.\n"; return true;
    }

    bool deleteById(int id){
        auto before = events.size();
        events.erase(std::remove_if(events.begin(),events.end(),[&](const Event& e){return e.id==id;}),events.end());
        if (events.size()==before){ std::cout<<"No event with that ID.\n"; return false; }
        std::cout<<"Deleted.\n"; return true;
    }

    bool deleteByName(const std::string& name){
        auto before = events.size();
        events.erase(std::remove_if(events.begin(),events.end(),[&](const Event& e){return iequals(e.name,name);} ),events.end());
        if (events.size()==before){ std::cout<<"No event with that name.\n"; return false; }
        std::cout<<"Deleted.\n"; return true;
    }

    void dayView(const std::string& date){
        std::vector<Event> list; for (const auto& e: events) if (e.date==date) list.push_back(e);
        std::sort(list.begin(),list.end(),[](const Event&a,const Event&b){return toMinutes(a.time)<toMinutes(b.time);} );
        if (list.empty()){ std::cout<<"No events on this date.\n"; return; }
        printHeader(); for (const auto& e: list) printEvent(e);
    }

    void todaysEvents(){ dayView(today()); }

    void listAll(){
        if (events.empty()){ std::cout<<"No events.\n"; return; }
        std::vector<Event> list=events;
        auto ymd=[](const std::string& d){ return d.substr(6,4)+d.substr(3,2)+d.substr(0,2); };
        std::sort(list.begin(),list.end(),[&](const Event&a,const Event&b){ if (a.date==b.date) return toMinutes(a.time)<toMinutes(b.time); return ymd(a.date)<ymd(b.date);} );
        printHeader(); for (const auto& e: list) printEvent(e);
    }

    void search(const std::string& keyword){
        std::vector<Event> list; for (const auto& e: events){ if (icontains(e.name,keyword) || icontains(e.type,keyword)) list.push_back(e); }
        if (list.empty()){ std::cout<<"No matches.\n"; return; }
        std::sort(list.begin(),list.end(),[](const Event&a,const Event&b){return a.id<b.id;});
        printHeader(); for (const auto& e: list) printEvent(e);
    }

    void statistics(){
        std::cout<<"Total events: "<<events.size()<<"\n";
        std::map<std::string,int> byType, byDate; for (const auto& e: events){ byType[e.type]++; byDate[e.date]++; }
        std::cout<<"By type:\n"; for (auto&p: byType) std::cout<<"  "<<p.first<<": "<<p.second<<"\n";
        std::vector<std::pair<std::string,int>> v(byDate.begin(),byDate.end());
        std::sort(v.begin(),v.end(),[](auto&a,auto&b){return a.second>b.second;});
        std::cout<<"Top 5 dates by count:\n"; for(size_t i=0;i<v.size()&&i<5;i++) std::cout<<"  "<<v[i].first<<": "<<v[i].second<<"\n";
    }

    // ------------------- Reminders (Simulated) -------------------
    void loadAttendeesFromPaste(){
        std::cout<<"Paste emails (comma/space/newline separated). End with a blank line.\n";
        attendeeEmails.clear();
        std::string line, all;
        while (true){
            std::getline(std::cin,line);
            if (line.size()==0) break; all += line + ' ';
        }
        std::stringstream ss(all); std::string token;
        auto isEmail=[&](const std::string&s){ return s.find('@')!=std::string::npos && s.find('.')!=std::string::npos; };
        while (ss>>token){ if (token.back()==','||token.back()==';') token.pop_back(); if (isEmail(token)) attendeeEmails.push_back(token); }
        std::cout<<"Loaded "<<attendeeEmails.size()<<" attendee emails.\n";
    }

    void sendReminderForDate(const std::string& date){
        std::vector<Event> list; for (const auto& e: events) if (e.date==date) list.push_back(e);
        if (list.empty()){ std::cout<<"No events on this date.\n"; return; }
        std::sort(list.begin(),list.end(),[](const Event&a,const Event&b){return toMinutes(a.time)<toMinutes(b.time);} );
        std::ostringstream body; body<<"Upcoming events on "<<date<<":\n\n";
        for (const auto& e: list) body<<"- "<<e.time<<" | "<<e.name<<" ("<<e.type<<") @ "<<(e.location.empty()?"TBA":e.location)<<"\n";
        if (attendeeEmails.empty()){
            std::cout<<"No attendee emails loaded. Choose 'Load attendees' first.\n"; return;
        }
        std::cout<<"[SIMULATED EMAIL SEND] To "<<attendeeEmails.size()<<" recipients.\nSubject: Reminder: Events on "<<date<<"\n\n"<<body.str();
        std::cout<<"(Emails not actually sent in online IDE.)\n";
    }

    // ------------------- Suggestions -------------------
    void suggestSlots(const std::string& date, int duration=60){
        std::cout<<"Suggested available slots on "<<date<<":\n";
        std::vector<std::pair<int,int>> occ; for (const auto& e: events) if (e.date==date){ int s=toMinutes(e.time); occ.push_back({s,s+60}); }
        std::sort(occ.begin(),occ.end());
        int start=8*60, end=20*60, shown=0;
        for (int t=start; t+duration<=end && shown<5; t+=30){ bool clash=false; for (auto& iv: occ){ if (!(t+duration<=iv.first || t>=iv.second)) { clash=true; break; } } if(!clash){ std::cout<<"  - "<<fromMinutes(t)<<" to "<<fromMinutes(t+duration)<<"\n"; shown++; } }
        if (!shown) std::cout<<"  (No free 1-hour slots found in working window)\n";
    }

    // ------------------- Snapshot (manual persistence aid) -------------------
    void exportSnapshotCSV(){
        std::cout<<"id,name,date,time,type,location\n";
        for (const auto& e: events){
            std::cout<<e.id<<","<<e.name<<","<<e.date<<","<<e.time<<","<<e.type<<","<<e.location<<"\n";
        }
        std::cout<<"(Copy the above lines to save. Import with the menu option.)\n";
    }

    void importSnapshotCSV(){
        std::cout<<"Paste CSV lines (header optional). End with a blank line.\n";
        std::string line; std::vector<Event> temp; int maxId=0; bool first=true;
        while (true){
            std::getline(std::cin,line); if (line.size()==0) break; if (line.find(",")==std::string::npos) continue;
            if (first && toLower(line).find("id,name,date,time,type,location")!=std::string::npos){ first=false; continue; }
            first=false;
            std::stringstream ss(line); std::string tok; Event e; int col=0;
            while (std::getline(ss,tok,',')){
                switch(col){
                    case 0: if (!tok.empty()) e.id = std::stoi(tok); break;
                    case 1: e.name = tok; break;
                    case 2: e.date = tok; break;
                    case 3: e.time = tok; break;
                    case 4: e.type = tok; break;
                    case 5: e.location = tok; break;
                }
                col++;
            }
            if (e.id==0 || e.name.empty() || !isValidDate(e.date) || !isValidTime(e.time)) continue;
            temp.push_back(e); maxId=std::max(maxId,e.id);
        }
        if (temp.empty()){ std::cout<<"Nothing imported.\n"; return; }
        events = temp; nextId = maxId+1; std::cout<<"Imported "<<events.size()<<" events. Next ID: "<<nextId<<"\n";
    }
};

// ------------------- CLI -------------------

static bool isAdmin = false;

void adminLogin(){
    std::string user, pass; std::cout<<"\n== Admin Login ==\nUsername: "; std::getline(std::cin,user); std::cout<<"Password: "; std::getline(std::cin,pass);
    if ((user=="admin" || user=="ACMadmin") && pass=="admin123") { isAdmin=true; std::cout<<"Logged in as admin.\n"; }
    else std::cout<<"Invalid credentials. Continuing as viewer.\n";
}

void menu(){
    std::cout<<"\n====== Smart Event Manager ======\n";
    std::cout<<"1) List all events\n";
    std::cout<<"2) Day view (pick date)\n";
    std::cout<<"3) Today's events\n";
    std::cout<<"4) Search events\n";
    if (isAdmin){
        std::cout<<"5) Add event (admin)\n";
        std::cout<<"6) Edit event by ID (admin)\n";
        std::cout<<"7) Delete event by ID (admin)\n";
        std::cout<<"8) Delete event by name (admin)\n";
        std::cout<<"9) Load attendees (paste emails) (admin)\n";
        std::cout<<"10) Send reminders (admin)\n";
        std::cout<<"11) Statistics (admin)\n";
        std::cout<<"12) Export snapshot CSV (admin)\n";
        std::cout<<"13) Import snapshot CSV (admin)\n";
    }
    std::cout<<"0) Exit\nSelect: ";
}

int main(){
    EventManager mgr;

    std::cout<<"Login as admin? (y/N): "; std::string ans; std::getline(std::cin,ans); if (!ans.empty() && (ans=="y"||ans=="Y")) adminLogin();

    while (true){
        menu(); std::string choice; std::getline(std::cin,choice); if (choice=="0"||std::cin.eof()) break;
        if (choice=="1"){
            mgr.listAll();
        } else if (choice=="2"){
            std::string d; std::cout<<"Enter date (DD-MM-YYYY): "; std::getline(std::cin,d);
            if (!EventManager::isValidDate(d)){ std::cout<<"Invalid date.\n"; continue; }
            mgr.dayView(d);
        } else if (choice=="3"){
            mgr.todaysEvents();
        } else if (choice=="4"){
            std::string k; std::cout<<"Keyword (name/type): "; std::getline(std::cin,k); mgr.search(k);
        } else if (isAdmin && choice=="5"){
            std::string name,date,time,type,loc; std::cout<<"Name: "; std::getline(std::cin,name);
            std::cout<<"Date (DD-MM-YYYY): "; std::getline(std::cin,date);
            std::cout<<"Time (HH:MM 24h): "; std::getline(std::cin,time);
            std::cout<<"Type: "; std::getline(std::cin,type);
            std::cout<<"Location (optional): "; std::getline(std::cin,loc);
            mgr.addEvent(name,date,time,type,loc);
        } else if (isAdmin && choice=="6"){
            std::string s; std::cout<<"ID to edit: "; std::getline(std::cin,s);
            if (s.empty() || std::any_of(s.begin(),s.end(),[](char c){return !std::isdigit((unsigned char)c);})){ std::cout<<"Invalid ID.\n"; continue; }
            mgr.editEventById(std::stoi(s));
        } else if (isAdmin && choice=="7"){
            std::string s; std::cout<<"ID to delete: "; std::getline(std::cin,s);
            if (s.empty() || std::any_of(s.begin(),s.end(),[](char c){return !std::isdigit((unsigned char)c);})){ std::cout<<"Invalid ID.\n"; continue; }
            mgr.deleteById(std::stoi(s));
        } else if (isAdmin && choice=="8"){
            std::string n; std::cout<<"Name to delete: "; std::getline(std::cin,n); mgr.deleteByName(n);
        } else if (isAdmin && choice=="9"){
            mgr.loadAttendeesFromPaste();
        } else if (isAdmin && choice=="10"){
            std::string d; std::cout<<"Send reminders for date (DD-MM-YYYY): "; std::getline(std::cin,d);
            if (!EventManager::isValidDate(d)){ std::cout<<"Invalid date.\n"; continue; }
            mgr.sendReminderForDate(d);
        } else if (isAdmin && choice=="11"){
            mgr.statistics();
        } else if (isAdmin && choice=="12"){
            mgr.exportSnapshotCSV();
        } else if (isAdmin && choice=="13"){
            mgr.importSnapshotCSV();
        } else {
            std::cout<<"Invalid choice."<<(isAdmin?" Try 0-13.":" Try 0-4.")<<"\n";
        }
    }

    std::cout<<"Goodbye!\n";
    return 0;
}