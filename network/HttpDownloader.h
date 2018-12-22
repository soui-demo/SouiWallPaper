#pragma once
#include <interface/STaskLoop-i.h>
#include <string>
#include <helper/SCriticalSection.h>

#define MAX_DOWNLOADER 10


SEVENT_BEGIN(EventDownloadFinish,EVT_DOWNLOAD_FINISH)
	std::string url;
	std::string data;
	long   type;
	long   catetory;
	CAutoRefPtr<IBitmap> pImg;
SEVENT_END()

enum{
	URL_CATEGORIES=1,
	URL_IMGINFO,
	URL_IMG,
};

class CHttpDownloader : public SSingleton<CHttpDownloader>
{
public:
	CHttpDownloader(void);
	~CHttpDownloader(void);

	void download(const std::string & url,long type, long catetory);

private:
	void _download(const std::string & url,long type, long catetory,int iLoop);
	string uri2md5(const string & uri);
private:
	SComMgr m_ComMgr;
	CAutoRefPtr<ITaskLoop> m_taskLoops[MAX_DOWNLOADER];
	
	SCriticalSection	m_cs;
	set<string>			m_pendingTasks;

	string m_strCachePath;
};
