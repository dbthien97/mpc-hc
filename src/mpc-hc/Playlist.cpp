/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2015, 2017 see Authors.txt
 *
 * This file is part of MPC-HC.
 *
 * MPC-HC is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-HC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

CPlaylistItem::CPlaylistItem()
    : m_posNextShuffle(nullptr)
    , m_posPrevShuffle(nullptr)
    , m_type(file)
    , m_duration(0)
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
    m_id = m_globalid++;
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
        m_fns.RemoveAll();
        m_fns.AddTailList(&pli.m_fns);
        m_subs.RemoveAll();
        m_subs.AddTailList(&pli.m_subs);
        m_type = pli.m_type;
        m_fInvalid = pli.m_fInvalid;
        m_duration = pli.m_duration;
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
        m_ydl_subs.RemoveAll();
        m_ydl_subs.AddHeadList(&pli.m_ydl_subs);
    }
    return *this;
}

POSITION CPlaylistItem::FindFile(LPCTSTR path)
{
    POSITION pos = m_fns.GetHeadPosition();
    while (pos) {
        if (m_fns.GetAt(pos).CompareNoCase(path) == 0) {
            return pos;
        }
        m_fns.GetNext(pos);
    }
    return nullptr;
}

CString CPlaylistItem::GetLabel(int i)
{
    CString str;

    if (i == 0) {
        ASSERT(m_label);
        if (m_label && !m_label.IsEmpty()) {
            str = m_label;
        } else if (!m_fns.IsEmpty()) {
            str = PathUtils::StripPathOrUrl(m_fns.GetHead());
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
    if (m_fns.IsEmpty()) {
        return;
    }

    const CAppSettings& s = AfxGetAppSettings();

    CString fn = m_fns.GetHead();

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
                HANDLE hFind = FindFirstFile(fn.Left(i) + _T("*.*"), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            continue;
                        }

                        CString fullpath = path + fd.cFileName;
                        CString ext2 = fullpath.Mid(fullpath.ReverseFind('.') + 1).MakeLower();
                        if (!FindStringInList(m_fns, fullpath) && ext != ext2
                                && mf.FindExt(ext2, true) && mf.IsUsingEngine(fullpath, DirectShow)) {
                            m_fns.AddTail(fullpath);
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
        int l  = dir.ReverseFind('/') + 1;
        int l2 = dir.ReverseFind('.');
        if (l2 < l) { // no extension, read to the end
            l2 = fn.GetLength();
        }
        CString title = dir.Mid(l, l2 - l);
        paths.Add(title);

        CAtlArray<Subtitle::SubFile> ret;
        Subtitle::GetSubFileNames(fn, paths, ret);

        for (size_t i = 0; i < ret.GetCount(); i++) {
            if (!FindStringInList(m_subs, ret[i].fn)) {
                m_subs.AddTail(ret[i].fn);
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
    const bool bWasPlaying = (m_pos != nullptr);
    __super::RemoveAll();
    m_pos = nullptr;
    m_posHeadShuffle = m_posTailShuffle = nullptr;
    m_nShuffledListSize = 0;
    m_shufflePlayed.clear();
    m_shuffleRecent.clear();
    m_shuffleHistory.clear();
    m_shuffleHistoryCursor = 0;
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
        CString& fn = GetAt(pos).m_fns.GetHead();
        a[i].str = (LPCTSTR)fn + std::max(fn.ReverseFind('/'), fn.ReverseFind('\\')) + 1;
        a[i].pos = pos;
    }
    std::sort(a.GetData(), a.GetData() + a.GetCount());
    for (size_t i = 0; i < a.GetCount(); i++) {
        MoveToTail(a[i].pos);
    }
}

void CPlaylist::SortByPath()
{
    CAtlArray<plsort2_t> a;
    a.SetCount(GetCount());
    POSITION pos = GetHeadPosition();
    for (int i = 0; pos; i++, GetNext(pos)) {
        a[i].str = GetAt(pos).m_fns.GetHead(), a[i].pos = pos;
    }
    std::sort(a.GetData(), a.GetData() + a.GetCount());
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
    const bool changed = m_bShuffle != bEnable;
    m_bShuffle = bEnable;
    if (changed) {
        m_shufflePlayed.clear();
        m_shuffleRecent.clear();
        m_shuffleHistory.clear();
        m_shuffleHistoryCursor = 0;
    }
    if (bEnable && m_pos) {
        RecordShuffle(m_pos);
    }

    // Retain MPC-HC's private shuffle links for compatibility, while never
    // changing the real CAtlList order displayed by the playlist control.
    if (bEnable && !IsEmpty()) {
        m_nShuffledListSize = GetCount();
        std::vector<POSITION> positions;
        positions.reserve(m_nShuffledListSize);
        for (POSITION pos = GetHeadPosition(); pos; GetNext(pos)) {
            positions.push_back(pos);
        }
        std::shuffle(positions.begin(), positions.end(), m_shuffleRng);
        m_posHeadShuffle = positions.front();
        m_posTailShuffle = positions.back();
        for (size_t i = 0; i < positions.size(); ++i) {
            CPlaylistItem& item = GetAt(positions[i]);
            item.m_posPrevShuffle = i ? positions[i - 1] : nullptr;
            item.m_posNextShuffle = (i + 1 < positions.size()) ? positions[i + 1] : nullptr;
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

// Smart Shuffle is separate from the visible playlist order.
// A file URL/path is used as the playback identity; duplicated playlist
// entries therefore do not cause the same video to play twice in one cycle.
std::wstring CPlaylist::ShuffleKey(POSITION pos)
{
    const CPlaylistItem& item = GetAt(pos);
    CString key;
    if (item.m_bYoutubeDL && !item.m_ydlSourceURL.IsEmpty()) {
        key = item.m_ydlSourceURL;
    } else if (!item.m_fns.IsEmpty()) {
        key = item.m_fns.GetHead();
    }
    if (key.IsEmpty()) {
        key.Format(_T("#item:%u"), item.m_id);
    } else if (!PathUtils::IsURL(key)) {
        key.Replace(_T('/'), _T('\\'));
        key.MakeLower();
    }
    if (item.m_cue) {
        CString suffix;
        suffix.Format(_T("|cue:%d"), item.m_cue_index);
        key += suffix;
    }
    return std::wstring(key.GetString());
}

POSITION CPlaylist::FindShuffleKey(const std::wstring& key)
{
    for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
        if (!GetAt(p).m_fInvalid && ShuffleKey(p) == key) {
            return p;
        }
    }
    return nullptr;
}

void CPlaylist::RecordShuffle(POSITION pos)
{
    if (!pos || GetAt(pos).m_fInvalid) {
        return;
    }
    const std::wstring key = ShuffleKey(pos);
    m_shufflePlayed.insert(key);

    // Previous/Next through existing history does not create a new entry.
    if (m_shuffleHistory.empty() || m_shuffleHistoryCursor >= m_shuffleHistory.size()
            || m_shuffleHistory[m_shuffleHistoryCursor] != key) {
        if (m_shuffleHistoryCursor + 1 < m_shuffleHistory.size()) {
            m_shuffleHistory.resize(m_shuffleHistoryCursor + 1);
        }
        m_shuffleHistory.push_back(key);
        m_shuffleHistoryCursor = m_shuffleHistory.size() - 1;
        if (m_shuffleHistory.size() > 100000) {
            m_shuffleHistory.erase(m_shuffleHistory.begin());
            --m_shuffleHistoryCursor;
        }
    }

    if (m_shuffleRecent.empty() || m_shuffleRecent.back() != key) {
        m_shuffleRecent.push_back(key);
        const size_t keep = (std::max)(size_t(20), size_t(GetCount()) * 2);
        while (m_shuffleRecent.size() > keep) {
            m_shuffleRecent.pop_front();
        }
    }
}

bool CPlaylist::IsSmartShuffleEnd()
{
    // A Forward operation after Previous must remain possible.
    if (!m_shuffleHistory.empty() && m_shuffleHistoryCursor + 1 < m_shuffleHistory.size()) {
        return false;
    }
    for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
        if (!GetAt(p).m_fInvalid && !m_shufflePlayed.count(ShuffleKey(p))) {
            return false;
        }
    }
    return true;
}

POSITION CPlaylist::NextSmartShuffle()
{
    if (!m_bShuffle) {
        return nullptr;
    }

    // Forward replays exact history after a Previous operation.
    while (!m_shuffleHistory.empty() && m_shuffleHistoryCursor + 1 < m_shuffleHistory.size()) {
        ++m_shuffleHistoryCursor;
        POSITION prior = FindShuffleKey(m_shuffleHistory[m_shuffleHistoryCursor]);
        if (prior) {
            return prior;
        }
    }

    std::vector<POSITION> candidates;
    std::set<std::wstring> uniqueKeys;
    for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
        if (!GetAt(p).m_fInvalid && uniqueKeys.insert(ShuffleKey(p)).second) {
            candidates.push_back(p);
        }
    }
    if (candidates.empty()) {
        return nullptr;
    }

    // Reset only after every available UNIQUE video has been played.
    if (IsSmartShuffleEnd()) {
        m_shufflePlayed.clear();
    }

    // Protect approximately the last 30% of a completed cycle from immediate
    // repetition at the beginning of a new cycle (when enough options exist).
    const size_t avoidCount = candidates.size() * 3 / 10;
    const size_t recentCount = (std::min)(avoidCount, m_shuffleRecent.size());
    std::set<std::wstring> blocked;
    for (size_t i = m_shuffleRecent.size() - recentCount; i < m_shuffleRecent.size(); ++i) {
        blocked.insert(m_shuffleRecent[i]);
    }

    std::shuffle(candidates.begin(), candidates.end(), m_shuffleRng);
    POSITION fallback = nullptr;
    POSITION sameAsCurrent = nullptr;
    const std::wstring current = m_pos ? ShuffleKey(m_pos) : std::wstring();
    for (POSITION p : candidates) {
        const std::wstring key = ShuffleKey(p);
        if (m_shufflePlayed.count(key)) {
            continue;
        }
        if (candidates.size() > 1 && key == current) {
            sameAsCurrent = p;
            continue;
        }
        if (!fallback) {
            fallback = p;
        }
        if (!blocked.count(key)) {
            return p;
        }
    }
    return fallback ? fallback : sameAsCurrent;
}

POSITION CPlaylist::PrevSmartShuffle()
{
    if (!m_bShuffle || m_shuffleHistory.empty() || m_shuffleHistoryCursor == 0) {
        return m_pos;
    }
    const size_t originalCursor = m_shuffleHistoryCursor;
    while (m_shuffleHistoryCursor > 0) {
        --m_shuffleHistoryCursor;
        POSITION previous = FindShuffleKey(m_shuffleHistory[m_shuffleHistoryCursor]);
        if (previous) {
            return previous;
        }
    }
    m_shuffleHistoryCursor = originalCursor;
    return m_pos;
}

// Sidecar file lives beside default.mpcpl, never touches playlist ordering.
void CPlaylist::SaveShuffleHistory(LPCTSTR path)
{
    if (!m_bShuffle) {
        ::DeleteFile(path);
        return;
    }
    try {
        CFile file(path, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive);
        CArchive ar(&file, CArchive::store);
        ar << DWORD(0x53534832);  // SSH2
        ar << DWORD(m_shufflePlayed.size());
        for (const auto& key : m_shufflePlayed) {
            ar << CString(key.c_str());
        }
        ar << DWORD(m_shuffleRecent.size());
        for (const auto& key : m_shuffleRecent) {
            ar << CString(key.c_str());
        }
        ar << DWORD(m_shuffleHistory.size());
        ar << DWORD(m_shuffleHistoryCursor);
        for (const auto& key : m_shuffleHistory) {
            ar << CString(key.c_str());
        }
        ar.Close();
    } catch (CException* e) {
        e->Delete();
    }
}

void CPlaylist::LoadShuffleHistory(LPCTSTR path)
{
    if (!m_bShuffle) {
        return;
    }
    try {
        CFile file(path, CFile::modeRead | CFile::shareDenyWrite);
        CArchive ar(&file, CArchive::load);
        DWORD magic = 0, count = 0;
        ar >> magic >> count;
        if (magic != 0x53534832 || count > 100000) {
            return;
        }
        std::set<std::wstring> available, played;
        std::deque<std::wstring> recent;
        std::vector<std::wstring> history;
        for (POSITION p = GetHeadPosition(); p; GetNext(p)) {
            if (!GetAt(p).m_fInvalid) {
                available.insert(ShuffleKey(p));
            }
        }
        for (DWORD i = 0; i < count; ++i) {
            CString key;
            ar >> key;
            if (available.count(key.GetString())) {
                played.insert(key.GetString());
            }
        }
        ar >> count;
        if (count > 100000) {
            return;
        }
        for (DWORD i = 0; i < count; ++i) {
            CString key;
            ar >> key;
            if (available.count(key.GetString())) {
                recent.push_back(key.GetString());
            }
        }
        DWORD historyCursor = 0;
        ar >> count >> historyCursor;
        if (count > 100000 || (count && historyCursor >= count)) {
            return;
        }
        size_t restoredCursor = 0;
        for (DWORD i = 0; i < count; ++i) {
            CString key;
            ar >> key;
            if (available.count(key.GetString())) {
                if (i <= historyCursor) {
                    restoredCursor = history.size();
                }
                history.emplace_back(key.GetString());
            }
        }
        m_shufflePlayed.swap(played);
        m_shuffleRecent.swap(recent);
        m_shuffleHistory.swap(history);
        m_shuffleHistoryCursor = m_shuffleHistory.empty() ? 0 :
            (std::min)(restoredCursor, m_shuffleHistory.size() - 1);
        if (m_pos) {
            RecordShuffle(m_pos);
        }
    } catch (CException* e) {
        e->Delete();
    }
}

