#include "StdAfx.h"
#include "HttpDownloader.h"
#include <helper/SFunctor.hpp>
#include <WinInet.h>
#pragma comment(lib, "WinInet.lib")
#include "MD5.h"

template<>
CHttpDownloader * SSingleton<CHttpDownloader>::ms_Singleton = NULL;

CHttpDownloader::CHttpDownloader(void)
{
	SNotifyCenter::getSingletonPtr()->addEvent(EVENTID(EventDownloadFinish));


	char szCurDir[MAX_PATH];
	GetCurrentDirectoryA(MAX_PATH, szCurDir);
	m_strCachePath = szCurDir;
	m_strCachePath += "\\image_cache";
	CreateDirectoryA(m_strCachePath.c_str(),NULL);

	for(int i=0;i<MAX_DOWNLOADER;i++)
	{
		m_ComMgr.CreateTaskLoop((IObjRef **)&m_taskLoops[i]);
		char szName[30];
		sprintf(szName,"downloader_%d",i+1);
		m_taskLoops[i]->start(szName,ITaskLoop::Low);
	}
}

CHttpDownloader::~CHttpDownloader(void)
{
	for(int i=0;i<MAX_DOWNLOADER;i++)
	{
		int nPendingTasks = m_taskLoops[i]->getTaskCount();
		SLOG_INFO("downloader "<<i<<" remain "<<nPendingTasks<<" tasks");
		m_taskLoops[i]->stop();
		m_taskLoops[i] = NULL;
	}
}

void CHttpDownloader::download(const std::string & url,long type, long catetory)
{
	int nMinTasks = 10000;
	int iLoop = -1;
	{
		SAutoLock lock(m_cs);
		if(m_pendingTasks.find(url)!=m_pendingTasks.end())
			return;
		m_pendingTasks.insert(url);
	}
	for(int i=0;i<MAX_DOWNLOADER;i++)
	{
		int nTasks = m_taskLoops[i]->getTaskCount();
		if(nMinTasks>nTasks)
		{
			nMinTasks = nTasks;
			iLoop = i;
		}
	}
	STaskHelper::post(m_taskLoops[iLoop],this,&CHttpDownloader::_download,url,type,catetory,iLoop,false);
}


string CHttpDownloader::uri2md5(const string & uri)
{
	MD5_CTX ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char *)uri.c_str(), uri.length());
	unsigned char digest[16];
	MD5Final(&ctx, digest);

	char szMD5[16 * 2 + 1];
	for (int i = 0; i<16; i++)
	{
		sprintf(szMD5 + i * 2, "%x", (digest[i] & 0xF0) >> 4);
		sprintf(szMD5 + i * 2 + 1, "%x", digest[i] & 0x0F);
	}
	return string(szMD5, 32);
}

void CHttpDownloader::_download(const std::string & url,long type, long category,int iLoop)
{
	SLOG_INFO("_download, url:"<<url.c_str()<<" type:"<<type<<" category");

	string data;

	string strUriMd5 = uri2md5(url);
	string strCacheFile = m_strCachePath + "\\" + strUriMd5 + ".cache";

	//check local cache
	bool bIsNeedDownLoad = true;
	if (type == URL_IMG)
	{
		FILE *f = fopen(strCacheFile.c_str(), "rb");
		if (f)
		{
			fseek(f, 0, SEEK_END);
			int nLen = ftell(f);
			fseek(f, 0, SEEK_SET);
			char *pBuf = new char[nLen];
			fread(pBuf, 1, nLen, f);
			data = string(pBuf, nLen);
			delete[]pBuf;
			fclose(f);
			bIsNeedDownLoad = false;
			SLOG_INFO("load image from cache uri: " << url.c_str());
		}
	}

	if(bIsNeedDownLoad)
	{
		// 打开http链接
		HINTERNET hConnect = InternetOpen(NULL, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0); 

		SLOG_INFO("<<<<<start download "<<url.c_str());
		if (hConnect)
		{
			DWORD dwTimeOut = 0;
			InternetSetOption(hConnect, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeOut, sizeof(dwTimeOut));

			SStringW wUrl = S_CA2W(url.c_str());
			HINTERNET hSession = InternetOpenUrl(hConnect, wUrl, NULL, 0, INTERNET_FLAG_TRANSFER_BINARY | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
			if (hSession)
			{
				// 建立数据缓冲区
				DWORD dwRead = 0;
				DWORD dwBuffer = 1024 * 1024;
				char *szBuffer = new char[dwBuffer];
				memset(szBuffer, 0, dwBuffer);

				
				while(InternetReadFile(hSession, szBuffer, dwBuffer, &dwRead) && (dwRead > 0) && m_taskLoops[iLoop]->isRunning())
				{
					data += string(szBuffer,dwRead);
				}

				// 销毁数据缓冲区
				delete []szBuffer;
				szBuffer = NULL;

				InternetCloseHandle(hSession);
			}
			InternetCloseHandle(hConnect);

		}
		SLOG_INFO(">>>>> end download "<<url.c_str());
	}
	if(m_taskLoops[iLoop]->isRunning())
	{
		{
			SAutoLock lock(m_cs);
			m_pendingTasks.erase(url);
		}

		EventDownloadFinish *evt = new EventDownloadFinish(NULL);
		evt->url = url;
		evt->data = data;
		evt->type = type;
		evt->catetory = category;
		if(type == URL_IMG)
		{//try to decode image.
			evt->pImg.Attach(SResLoadFromMemory::LoadImage((LPVOID)data.c_str(),data.size()));
			if(evt->pImg && bIsNeedDownLoad)
			{
				FILE *f = fopen(strCacheFile.c_str(), "wb");
				if (f)
				{
					fwrite(data.c_str(), 1, data.length(), f);
					fclose(f);
				}
			}
		}else
		{
			SLOG_INFO("download form "<<url.c_str()<<" result:"<<data.c_str());
		}
		SNotifyCenter::getSingletonPtr()->FireEventAsync(evt);
		evt->Release();
	}
}
