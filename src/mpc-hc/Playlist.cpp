
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "mplayerc.h"
#include "Playlist.h"
#include "PathUtils.h"
#include "SettingsDefines.h"
#include <algorithm>

//
// CPlaylistItem
//

UINT CPlaylistItem::m_globalid = 0;

CPlaylistItem::CPlaylistItem(bool is_copy /*= false*/)
    : m_posNextShuffle(nullptr)
    , m_posPrevShuffle(nullptr)
    , m_type(file)
    , m_duration(0)
    , m_filetime(0)
    , m_vinput(-1)
    , m_vchannel(-1)
    , m_ainput(-1)
    , m_country(0)
    , m_fns()
    , m_subs()
    , m_fInvalid(false)
    , m_label(_T(""))
    , m_cue(false)
    , m_cue_filename(_T(""))
    , m_cue_index(0)
    , m_cover(_T(""))
    , m_bYoutubeDL(false)
    , m_ydlSourceURL(_T(""))
    , m_useragent(_T(""))
    , m_ydl_subs()
    , inlineEditMaxWidth(0)
{
    if (is_copy) {
        m_id = 0;
    } else {
        m_id = ++m_globalid;
    }
}

CPlaylistItem::~CPlaylistItem()
{
}

CPlaylistItem::CPlaylistItem(const CPlaylistItem& pli)
{
    *this = pli;
}

CPlaylistItem& CPlaylistItem::operator=(const CPlaylistItem& pli)
{
    if (this != &pli) {
        m_id = pli.m_id;
        m_label = pli.m_label;
        m_fns = pli.m_fns;
        m_subs = pli.m_subs;
        m_type = pli.m_type;
        m_fInvalid = pli.m_fInvalid;
        m_duration = pli.m_duration;
        m_filetime = pli.m_filetime;
        m_vinput = pli.m_vinput;
        m_vchannel = pli.m_vchannel;
        m_ainput = pli.m_ainput;
        m_country = pli.m_country;
        m_posNextShuffle = pli.m_posNextShuffle;
        m_posPrevShuffle = pli.m_posPrevShuffle;
        m_cue = pli.m_cue;
        m_cue_filename = pli.m_cue_filename;
        m_cue_index = pli.m_cue_index;
        m_cover = pli.m_cover;
        m_bYoutubeDL = pli.m_bYoutubeDL;
        m_ydlSourceURL = pli.m_ydlSourceURL;
        m_useragent = pli.m_useragent;
        m_ydl_subs = pli.m_ydl_subs;
    }
    return *this;
}

bool CPlaylistItem::FindFile(LPCTSTR path)
{
    for (const CString& fn : m_fns) {
        if (fn.CompareNoCase(path) == 0) {
            return true;
        }
    }
    return false;
}

CString CPlaylistItem::GetLabel(int i)
{
    CString str;

    if (i == 0) {
        ASSERT(m_label);
        if (m_label && !m_label.IsEmpty()) {
            str = m_label;
        } else if (!m_fns.empty()) {
            str = PathUtils::StripPathOrUrl(m_fns.front());
        }
    } else if (i == 1) {
        if (m_fInvalid) {
            return _T("Invalid");
        }

        if (m_type == file) {
            REFERENCE_TIME rt = m_duration;

            if (rt > 0) {
                rt = (rt + 5000000) / 10000000;
                int ss = int(rt % 60);
                rt /= 60;
                int mm = int(rt % 60);
                rt /= 60;
                int hh = int(rt);

                str.Format(_T("%02d:%02d:%02d"), hh, mm, ss);
            }
        } else if (m_type == device) {
            // TODO
        }

    }

    return str;
}

void CPlaylistItem::AutoLoadFiles()
{
    if (m_fns.empty()) {
        return;
    }

    const CAppSettings& s = AfxGetAppSettings();

    CString fn = m_fns.front();

    if (s.fAutoloadAudio && !PathUtils::IsURL(fn) && (s.fEnableAudioSwitcher || s.SelectedAudioRenderer() != AUDRNDT_INTERNAL && s.SelectedAudioRenderer() != AUDRNDT_MPC)) {
        int i = fn.ReverseFind('.');
        if (i > 0) {
            const CMediaFormats& mf = s.m_Formats;

            CString ext = fn.Mid(i + 1).MakeLower();

            if (!mf.FindExt(ext, true)) {
                CString path = fn;
                path.Replace('/', '\\');
                path = path.Left(path.ReverseFind('\\') + 1);

                WIN32_FIND_DATA fd;
                ZeroMemory(&fd, sizeof(WIN32_FIND_DATA));
                HANDLE hFind = FindFirstFile(PathUtils::StripExtensionAndRarVolumeSuffix(fn) + _T("*.*"), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            continue;
                        }

                        CString fullpath = path + fd.cFileName;
                        CString ext2 = fullpath.Mid(fullpath.ReverseFind('.') + 1).MakeLower();
                        if (!FindStringInList(m_fns, fullpath) && ext != ext2
                                && mf.FindExt(ext2, true) && mf.IsUsingEngine(fullpath, DirectShow)) {
                            m_fns.push_back(fullpath);
                        }
                    } while (FindNextFile(hFind, &fd));

                    FindClose(hFind);
                }
            }
        }
    }

    if (s.IsISRAutoLoadEnabled()) {
        const CString& pathList = s.strSubtitlePaths;

        CAtlArray<CString> paths;

        int pos = 0;
        do {
            CString path = pathList.Tokenize(_T(";"), pos);
            if (!path.IsEmpty()) {
                paths.Add(path);
            }
        } while (pos != -1);

        CString dir = fn;
        dir.Replace('\\', '/');
        int l = dir.ReverseFind('/') + 1;
        CString title = PathUtils::StripExtensionAndRarVolumeSuffix(dir.Mid(l));
        paths.Add(title);

        CAtlArray<Subtitle::SubFile> ret;
        Subtitle::GetSubFileNames(fn, paths, ret);

        for (size_t i = 0; i < ret.GetCount(); i++) {
            if (!FindStringInList(m_subs, ret[i].fn)) {
                m_subs.push_back(ret[i].fn);
            }
        }
    }
}

//
// CPlaylist
//

CPlaylist::CPlaylist(bool bShuffle /*= false*/)
    : m_pos(nullptr)
    , m_bShuffle(bShuffle)
    , m_posHeadShuffle(nullptr)
    , m_posTailShuffle(nullptr)
    , m_nShuffledListSize(0)
{
}

CPlaylist::~CPlaylist()
{
}

bool CPlaylist::RemoveAll()
{
    m_shufflePlayed.clear();
    m_shuffleRecent.clear();
    __super::RemoveAll();
    bool bWasPlaying = (m_pos != nullptr);
    m_pos = nullptr;
    m_posHeadShuffle = m_posTailShuffle = nullptr;
    m_nShuffledListSize = 0;
    return bWasPlaying;
}

bool CPlaylist::RemoveAt(POSITION pos)
{
    if (pos) {
        // Update the shuffled list only if there is no pending reshuffle
        if (m_bShuffle && m_nShuffledListSize == GetCount()) {
            const CPlaylistItem& pli = GetAt(pos);
            if (pos == m_posHeadShuffle) {
                m_posHeadShuffle = pli.m_posNextShuffle;
            } else {
                GetAt(pli.m_posPrevShuffle).m_posNextShuffle = pli.m_posNextShuffle;
            }
            if (pos == m_posTailShuffle) {
                m_posTailShuffle = pli.m_posPrevShuffle;
            } else {
                GetAt(pli.m_posNextShuffle).m_posPrevShuffle = pli.m_posPrevShuffle;
            }
            m_nShuffledListSize--;
        }
        // Actually remove the item
        __super::RemoveAt(pos);
        // Check if it was the currently playing item
        if (m_pos == pos) {
            m_pos = nullptr;
            return true;
        }
    }

    return false;
}

struct plsort_t {
    UINT n;
    POSITION pos;

    bool operator <(const plsort_t& rhs) const {
        return n < rhs.n;
    }
};

struct plsort2_t {
    LPCTSTR  str;
    POSITION pos;

    bool operator <(const plsort2_t& rhs) const {
        return StrCmpLogicalW(str, rhs.str) < 0;
    }
};

void CPlaylist::SortById()
{
    CAtlArray<plsort_t> a;
    a.SetCount(GetCount());
    POSITION pos = GetHeadPosition();
    for (int i = 0; pos; i++, GetNext(pos)) {
        a[i].n = GetAt(pos).m_id, a[i].pos = pos;
    }
    std::sort(a.GetData(), a.GetData() + a.GetCount());
    for (size_t i = 0; i < a.GetCount(); i++) {
        MoveToTail(a[i].pos);
    }
}

void CPlaylist::SortByName()
{
    CAtlArray<plsort2_t> a;
    a.SetCount(GetCount());
    POSITION pos = GetHeadPosition();
    for (int i = 0; pos; i++, GetNext(pos)) {
        CString& fn = GetAt(pos).m_fns.front();
        a[i].str = (LPCTSTR)fn + std::max(fn.ReverseFind('/'), fn.ReverseFind('\\')) + 1;
        a[i].pos = pos;
    }
    std::sort(a.GetData(), a.GetData() + a.GetCount());
    for (size_t i = 0; i < a.GetCount(); i++) {
        MoveToTail(a[i].pos);
    }
}

// Sorts the items from startIndex until the end of the list, leaving the preceding items untouched.
void CPlaylist::SortByPath(int startIndex)
{
    if (startIndex < 0) {
        startIndex = 0;
    }
    if ((size_t)startIndex + 1 >= GetCount()) { // nothing to sort
        return;
    }

    POSITION pos = GetHeadPosition();
    for (int i = 0; i < startIndex; i++) {
        GetNext(pos);
    }

    CAtlArray<plsort2_t> a;
    a.SetCount(GetCount() - startIndex);
    for (int i = 0; pos; i++, GetNext(pos)) {
        a[i].str = GetAt(pos).m_fns.front(), a[i].pos = pos;
    }
    std::sort(a.GetData(), a.GetData() + a.GetCount());
    for (size_t i = 0; i < a.GetCount(); i++) {
        MoveToTail(a[i].pos);
    }
}

struct plsort3_t {
    ULONGLONG time;
    POSITION pos;
};

void CPlaylist::SortByDate(bool bIncreasing)
{
    CAtlArray<plsort3_t> a;
    a.SetCount(GetCount());
    POSITION pos = GetHeadPosition();
    for (int i = 0; pos; i++, GetNext(pos)) {
        CPlaylistItem& pli = GetAt(pos);
        if (pli.m_filetime == 0 && pli.m_type == CPlaylistItem::file && !pli.m_fns.empty()) {
            CString& fn = pli.m_fns.front();
            if (!PathUtils::IsURL(fn)) {
                pli.m_filetime = 1; // assume unavailable, so we don't check the file again
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesEx(fn, GetFileExInfoStandard, &fad)) {
                    ULARGE_INTEGER ft;
                    ft.LowPart = fad.ftCreationTime.dwLowDateTime;
                    ft.HighPart = fad.ftCreationTime.dwHighDateTime;
                    if (ft.QuadPart > 1) {
                        pli.m_filetime = ft.QuadPart;
                    }
                }
            }
        }
        a[i].time = pli.m_filetime;
        a[i].pos = pos;
    }
    // items without a valid time (URLs, missing files) always go to the bottom
    std::stable_sort(a.GetData(), a.GetData() + a.GetCount(), [bIncreasing](const plsort3_t& lhs, const plsort3_t& rhs) {
        if (lhs.time <= 1 || rhs.time <= 1) {
            return rhs.time <= 1 && lhs.time > 1;
        }
        return bIncreasing ? lhs.time < rhs.time : lhs.time > rhs.time;
    });
    for (size_t i = 0; i < a.GetCount(); i++) {
        MoveToTail(a[i].pos);
    }
}

void CPlaylist::Randomize()
{
    CAtlArray<plsort_t> a;
    a.SetCount(GetCount());
    srand((unsigned int)time(nullptr));
    POSITION pos = GetHeadPosition();
    for (int i = 0; pos; i++, GetNext(pos)) {
        a[i].n = rand(), a[i].pos = pos;
    }
    std::sort(a.GetData(), a.GetData() + a.GetCount());
    for (size_t i = 0; i < a.GetCount(); i++) {
        MoveToTail(a[i].pos);
    }
}

POSITION CPlaylist::GetPos() const
{
    return m_pos;
}

void CPlaylist::SetPos(POSITION pos)
{
    m_pos = pos;
    if (m_bShuffle && pos) {
        RecordShuffle(pos);
    }
}

POSITION CPlaylist::GetShuffleAwareHeadPosition()
{
    POSITION posHead;
    if (m_bShuffle) {
        ReshuffleIfNeeded();
        posHead = m_posHeadShuffle;
    } else {
        posHead = GetHeadPosition();
    }
    return posHead;
}

POSITION CPlaylist::GetShuffleAwareTailPosition()
{
    POSITION posTail;
    if (m_bShuffle) {
        ReshuffleIfNeeded();
        posTail = m_posTailShuffle;
    } else {
        posTail = GetTailPosition();
    }
    return posTail;
}

CPlaylistItem& CPlaylist::GetNextWrap(POSITION& pos)
{
    if (m_bShuffle) {
        ReshuffleIfNeeded();
        pos = GetAt(pos).m_posNextShuffle;
    } else {
        GetNext(pos);
    }
    if (!pos) {
        pos = GetShuffleAwareHeadPosition();
    }
    return GetAt(pos);
}

CPlaylistItem& CPlaylist::GetPrevWrap(POSITION& pos)
{
    if (m_bShuffle) {
        ReshuffleIfNeeded();
        pos = GetAt(pos).m_posPrevShuffle;
    } else {
        GetPrev(pos);
    }
    if (!pos) {
        pos = GetShuffleAwareTailPosition();
    }
    return GetAt(pos);
}

// Calling this function with bEnable equals to true when
// shuffle is already enabled will re-shuffle the tracks.
void CPlaylist::SetShuffle(bool bEnable)
{
    if (m_bShuffle != bEnable) {
        m_shufflePlayed.clear();
        m_shuffleRecent.clear();
    }
    m_bShuffle = bEnable;
    if (bEnable && m_pos) {
        RecordShuffle(m_pos);
    }

    if (bEnable && !IsEmpty()) {
        m_nShuffledListSize = GetCount();
        std::vector<POSITION> positions;
        positions.reserve(GetCount());
        POSITION pos = GetHeadPosition();
        while (pos) {
            positions.push_back(pos);
            GetNext(pos);
        }
        std::shuffle(positions.begin(), positions.end(), m_shuffleRng);
        m_posHeadShuffle = positions.front();
        m_posTailShuffle = positions.back();
        for (size_t i = 0; i < positions.size(); ++i) {
            CPlaylistItem& pli = GetAt(positions[i]);
            pli.m_posPrevShuffle = i ? positions[i - 1] : nullptr;
            pli.m_posNextShuffle = i + 1 < positions.size() ? positions[i + 1] : nullptr;
        }
    } else {
        m_posHeadShuffle = m_posTailShuffle = nullptr;
        m_nShuffledListSize = 0;
    }
}

CPlaylistIDs CPlaylist::GetIDs() {
    CPlaylistIDs ids;
    POSITION pos = GetHeadPosition();
    for (size_t i = 0; pos; i++, GetNext(pos)) {
        CPlaylistItem& pli = GetAt(pos);
        ids.push_back(pli.m_id);
    }
    return ids;
}

// This will reshuffle if the shuffled list size
// does not match the playlist size.
bool CPlaylist::ReshuffleIfNeeded()
{
    if (m_bShuffle && m_nShuffledListSize != GetCount()) {
        SetShuffle(true);
        return true;
    }
    return false;
}

// Smart Shuffle: the visible ATL list is never reordered.
std::wstring CPlaylist::ShuffleKey(POSITION pos)
{
    const auto& item = GetAt(pos);
    CString key;
    if (item.m_bYoutubeDL && !item.m_ydlSourceURL.IsEmpty()) {
        key = item.m_ydlSourceURL;
    } else if (!item.m_fns.empty()) {
        key = item.m_fns.front();
    }
    if (key.IsEmpty()) {
        key.Format(L"#item:%u", item.m_id);
    } else if (!PathUtils::IsURL(key)) {
        key.Replace(L'/', L'\\');
        key.MakeLower();
    }
    if (item.m_cue) {
        CString suffix;
        suffix.Format(L"|cue:%d", item.m_cue_index);
        key += suffix;
    }
    return std::wstring(key.GetString());
}

void CPlaylist::RecordShuffle(POSITION pos)
{
    if (GetAt(pos).m_fInvalid) return;
    auto key = ShuffleKey(pos);
    m_shufflePlayed.insert(key);
    if (!m_shuffleRecent.empty() && m_shuffleRecent.back() == key) return;
    m_shuffleRecent.erase(std::remove(m_shuffleRecent.begin(), m_shuffleRecent.end(), key), m_shuffleRecent.end());
    m_shuffleRecent.push_back(key);
    while (m_shuffleRecent.size() > GetCount()) m_shuffleRecent.pop_front();
}

bool CPlaylist::IsSmartShuffleEnd()
{
    for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
        if (!GetAt(p).m_fInvalid && !m_shufflePlayed.count(ShuffleKey(p))) return false;
    }
    return true;
}

POSITION CPlaylist::NextSmartShuffle()
{
    std::vector<POSITION> candidates;
    std::set<std::wstring> unique;
    for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
        if (!GetAt(p).m_fInvalid && unique.insert(ShuffleKey(p)).second) candidates.push_back(p);
    }
    if (candidates.empty()) return nullptr;
    if (IsSmartShuffleEnd()) m_shufflePlayed.clear();
    const size_t limit = candidates.size() * 3 / 10;
    const size_t recentCount = (std::min)(m_shuffleRecent.size(), limit);
    std::set<std::wstring> blocked(m_shuffleRecent.end() - recentCount, m_shuffleRecent.end());
    std::shuffle(candidates.begin(), candidates.end(), m_shuffleRng);
    POSITION fallback = nullptr;
    POSITION currentFallback = nullptr;
    for (POSITION p : candidates) {
        auto key = ShuffleKey(p);
        if (m_shufflePlayed.count(key)) continue;
        if (m_pos && key == ShuffleKey(m_pos)) { currentFallback = p; continue; }
        if (!fallback) fallback = p;
        if (!blocked.count(key)) return p;
    }
    return fallback ? fallback : currentFallback;
}

POSITION CPlaylist::PrevSmartShuffle()
{
    for (auto it = m_shuffleRecent.rbegin(); it != m_shuffleRecent.rend(); ++it) {
        if (m_pos && *it == ShuffleKey(m_pos)) continue;
        for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
            if (!GetAt(p).m_fInvalid && ShuffleKey(p) == *it) return p;
        }
    }
    return m_pos;
}

// Local binary sidecar; no network access. Only restore keys present in the playlist.
void CPlaylist::SaveShuffleHistory(LPCTSTR path)
{
    if (!m_bShuffle) { ::DeleteFile(path); return; }
    try {
        CFile file(path, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive);
        CArchive ar(&file, CArchive::store);
        ar << DWORD(0x53534831) << DWORD(m_shufflePlayed.size());
        for (const auto& key : m_shufflePlayed) ar << CString(key.c_str());
        ar << DWORD(m_shuffleRecent.size());
        for (const auto& key : m_shuffleRecent) ar << CString(key.c_str());
        ar.Close();
    } catch (CException* e) { e->Delete(); }
}

void CPlaylist::LoadShuffleHistory(LPCTSTR path)
{
    if (!m_bShuffle) return;
    try {
        CFile file(path, CFile::modeRead | CFile::shareDenyWrite);
        CArchive ar(&file, CArchive::load);
        DWORD magic = 0, count = 0;
        ar >> magic >> count;
        if (magic != 0x53534831 || count > 1000000) return;
        std::set<std::wstring> available, played;
        std::deque<std::wstring> recent;
        for (POSITION p = GetHeadPosition(); p; GetNext(p)) available.insert(ShuffleKey(p));
        for (DWORD i = 0; i < count; ++i) {
            CString key; ar >> key;
            if (available.count(key.GetString())) played.insert(key.GetString());
        }
        ar >> count;
        if (count > 1000000) return;
        for (DWORD i = 0; i < count; ++i) {
            CString key; ar >> key;
            if (available.count(key.GetString())) recent.push_back(key.GetString());
        }
        m_shufflePlayed.swap(played);
        m_shuffleRecent.swap(recent);
        if (m_pos) RecordShuffle(m_pos);
    } catch (CException* e) { e->Delete(); }
}
