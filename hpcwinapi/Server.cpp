#include <Windows.h>
#include <time.h>
#include <list>
#pragma comment(lib, "ws2_32.lib")

using namespace std;


//����� �������� ����
HWND hWindow;

//����� ������ � �����������
HWND hLabelPercentComplete;

//�������� � ���������
HWND hClientListBox;
//������ �������
HWND hBtn;

//������ ����������� ���������
HWND fromTB, toTB, stepTB, limTB;

//����� �������� � ������
HANDLE hColorThread = 0;
HANDLE hAcceptThread = 0;

#define C_COLOR 1
#define BTN_PRESS 2

//���� ����, ��� ���������� � ��������
bool running = 0;

//������� ��������� � ��������
double from, to, step, lim;

//��������� �������
void serverMain();
void acceptThread(void* param);
void clientThread(void* param);
void setComplete(int perc, int sec);

//����� ��������
int clientCount = 0;
//������ �������
int totalQueSize = 0;

//�������� ��������� ����������
double result = 0;

int lgefunc(int)
{
	int korn = 0;
		korn++;

	return 0;
}

//��������� � ������� ������� ��� ���������� � ������
typedef struct _clientData
{
	SOCKET s;
	SOCKADDR_IN addr;
} clientData;

//��������� ���������
typedef struct _Integral
{
	double from;
	double to;
	double step;
	double load;
} Integral;

//����� ������ �����������
SOCKET serverSocket;
sockaddr_in serverSaddr;

//������� ������
list<Integral> que;

//���� ������ ��� �������
CRITICAL_SECTION listBoxCriticalSection;
CRITICAL_SECTION queRetreiveCriticalSection;
CRITICAL_SECTION addResCriticalSection;

//��������� ��������� ���������� ����� ����������� ������
void addResSafe(double val)
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	result += val;
	LeaveCriticalSection(&queRetreiveCriticalSection);


}
//�������, ������� ����� �������� ����� �����������
double countOperations()
{
	double lims = to - from;
	return lims / step;
}

//�������� ��������� �������� ��� ����, ���� ������� ���������
Integral getNextIg()
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	if (que.size() > 0)
	{

		Integral ig = que.front();
		que.pop_front();
		LeaveCriticalSection(&queRetreiveCriticalSection);
		return ig;
	}
	//���� ������� ���������, ������ ����
	Integral r = { 0 };
	LeaveCriticalSection(&queRetreiveCriticalSection);

	return r;
}

//�������� ������ ���������� ������� ������
int getQueSizeSafe()
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	int res = que.size();
	LeaveCriticalSection(&queRetreiveCriticalSection);
	return res;
}

//��������� ������ � ����� �������, ���������
void addLastIg(Integral* ig)
{
	EnterCriticalSection(&queRetreiveCriticalSection);
	que.push_back(*ig);
	LeaveCriticalSection(&queRetreiveCriticalSection);

}


//����, ��������� ����������
void timerThread()
{
	while (running)
	{
		//�������� ���������� ���������� ������
		int cnt = totalQueSize - getQueSizeSafe();
		if (totalQueSize == 0)
			totalQueSize = 1;
		double perc = ((double)100 / totalQueSize) * cnt;
		//� ��� ���
		int newcnt = totalQueSize - getQueSizeSafe();

		//�������� �������
		int diffr = newcnt - cnt;
		//�������� ����� ���������� ����� ������
		long start = clock();
		while (diffr < 1)
		{
			Sleep(200);
			newcnt = totalQueSize - getQueSizeSafe();
			diffr = newcnt - cnt;
			if (!running)
			{
				return;
			}
		}
		long timer = clock();
		newcnt = totalQueSize - getQueSizeSafe();
		//������������ ���������� ����� ������ � ��������
		double duration = (timer - start) / (double)CLOCKS_PER_SEC;
		//���������� ������ � �������
		double packetspersecond = (double)diffr / duration;


		//������ ��������
		setComplete((int)perc, (int)((double)getQueSizeSafe() / packetspersecond));
	}
}


//��������� �������
list<Integral> formOperations()
{
	//�� 5% ���� �������� ����� ������ ������ ������
	unsigned long long ops = countOperations();
	totalQueSize = 0;
	ops++;
	unsigned  long long opsPerClient = ops * (double)0.05;
	if (opsPerClient >= 1000000)
	{
		opsPerClient = 1000000;
	}

	list<Integral> igs;
	double lastfrom = from, lastto = lastfrom;

	double slice = opsPerClient * step;
	while (ops > 0)
	{
		if (ops <= opsPerClient)
		{
			//slice = ops;
			opsPerClient = ops;
		}
		Integral in = { 0 };
		in.step = step;
		in.load = lim;
		in.from = lastfrom;
		in.to = lastfrom + slice;
		lastfrom += slice;
		igs.push_back(in);

		ops -= opsPerClient;
		totalQueSize++;
	}
	return igs;
}

//��������� ������� � ������
void addClientToListBox(sockaddr_in saddr) {
	char buf[32];
	char temp[32];
	TCHAR wbuf[32] = { 0 };
	strcat(strcat(strcat(strcpy(buf, inet_ntoa(saddr.sin_addr)), ":"), itoa(ntohs(saddr.sin_port), temp, 10)), " ");
	mbstowcs(wbuf, buf, 30);
	EnterCriticalSection(&listBoxCriticalSection);
	SendMessage(hClientListBox, LB_ADDSTRING, 0, (LPARAM)wbuf);
	LeaveCriticalSection(&listBoxCriticalSection);

}
//������� ������� �� ������
void deleteClientFromListBox(sockaddr_in saddr) {
	char buf[32];
	TCHAR wbuf[32];
	char temp[32];
	mbstowcs(wbuf, strcat(strcat(strcpy(buf, inet_ntoa(saddr.sin_addr)), ":"), itoa(ntohs(saddr.sin_port), temp, 10)), 32);

	EnterCriticalSection(&listBoxCriticalSection);
	int num = SendMessage(hClientListBox, LB_FINDSTRING, -1, (LPARAM)wbuf);
	SendMessage(hClientListBox, LB_DELETESTRING, num, 0);
	LeaveCriticalSection(&listBoxCriticalSection);
}

//������������� ����������
void stopMain()
{
	SetWindowText(hBtn, L"��");
	SetWindowLong(fromTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	SetWindowLong(toTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	SetWindowLong(stepTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	SetWindowLong(limTB, GWL_STYLE, WS_VISIBLE | WS_CHILD);
	running = 0;
	//TerminateThread(hAcceptThread, 0);
	closesocket(serverSocket);

	hAcceptThread = 0;
}
//��������� ���������, ������� � �����������
void preMain()
{
	result = 0;
	from = -1;
	to = -1;
	step = -1;
	lim = -1;
	TCHAR wfrom[25] = { 0 };
	TCHAR wto[25] = { 0 };
	TCHAR wstep[25] = { 0 };
	TCHAR wlim[25] = { 0 };

	GetWindowText(fromTB, wfrom, sizeof(wfrom));
	GetWindowText(toTB, wto, sizeof(wto));
	GetWindowText(stepTB, wstep, sizeof(wstep));
	GetWindowText(limTB, wlim, sizeof(wlim));




	if (wfrom[0] == 0 ||
		wto[0] == 0 ||
		wstep[0] == 0 ||
		wlim[0] == 0)
	{
		MessageBox(0, L"���� �� ���������� ����� ������� ��� �� �����", 0, 0);
		return;
	}

	from = _wtof(wfrom);
	to = _wtof(wto);
	step = _wtof(wstep);
	lim = _wtof(wlim);



	if (!(from != -1 && to != -1 && step != -1 && lim != -1))
	{
		MessageBox(0, L"���� �� ���������� ����� ������� ��� �� �����", 0, 0);
		return;
	}
	running = 1;

	SetWindowText(hBtn, L"����");
	SetWindowLong(fromTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);
	SetWindowLong(toTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);
	SetWindowLong(stepTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);
	SetWindowLong(limTB, GWL_STYLE, WS_VISIBLE | WS_CHILD | WS_DISABLED);


	que = formOperations();

	serverMain();
}

//������ �������
void serverMain()
{
	WSADATA wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		MessageBox(hWindow, L"������ �������� Winsock", L"������", MB_OK);
		return;
	}
	if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == SOCKET_ERROR) {
		MessageBox(hWindow, L"������ �������� ������", L"������", MB_OK);
		return;
	}
	serverSaddr.sin_family = AF_INET;
	serverSaddr.sin_addr.s_addr = INADDR_ANY;
	serverSaddr.sin_port = htons(1316);
	if (bind(serverSocket, (sockaddr*)&serverSaddr, sizeof(serverSaddr)) == SOCKET_ERROR) {
		MessageBox(hWindow, L"������ ���������� ������", L"������", MB_OK);
		return;
	}

	listen(serverSocket, 0x100);
	if (hAcceptThread == 0)
	{
		hAcceptThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)acceptThread, &serverSocket, 0, 0);
	}
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)timerThread, 0, 0, 0);

}

HANDLE* hClientWorkers = 0;
//����, ����������� �����������
void acceptThread(void* param)
{
	SOCKET s = *(SOCKET*)param;
	hClientWorkers = (HANDLE*)malloc(sizeof(HANDLE) * MAXIMUM_WAIT_OBJECTS);
	ZeroMemory(hClientWorkers, sizeof(HANDLE)* MAXIMUM_WAIT_OBJECTS);
	int currclient = 0;
	while (1)
	{
		sockaddr_in saddrClient = { 0 };
		int saddrSize = sizeof(sockaddr_in);
		SOCKET cl = accept(s, (sockaddr*)&saddrClient, &saddrSize);
		//������ �����������
		if (cl == INVALID_SOCKET)
		{
			if (running)
			{
				//������ ������, ������� �������� ��� ������, ������� ���� �����. ������ 10004 �������� ���������� �������� �� �������� ������, ��� �� �������� ������ ������� � ������ ������.
				if (WSAGetLastError() == 10004 && getQueSizeSafe() == 0)
				{
					break;
				}
				MessageBox(hWindow, L"������ �������", L"������", MB_OK);
			}
			return;
		}
		//��������� ��� � ������
		addClientToListBox(saddrClient);
		clientData cd = { 0 };
		//��������� ��� ������, ��� �������� ��� ������� ��� �� ������ � ������ ����
		cd.s = cl;
		cd.addr = saddrClient;

		//���� �������� �������������� ������, ��������� �������
		if (getQueSizeSafe() > 0)
		{
			hClientWorkers[currclient] = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)clientThread, &cd, 0, 0);
			currclient++;
		}
		//���� ������ ���������, ������������� ����� �����������
		else
			break;
	}
	//����������, ���� ��� ������� �������� ������
	WaitForMultipleObjects(currclient, hClientWorkers, 1, INFINITE);

	if (getQueSizeSafe() == 0)
	{
		//����� �����������
		TCHAR buf[320];
		setComplete((int)100, (int)0);
		swprintf(buf, L"�����! ��������: %f", result);
		stopMain();
		MessageBox(hWindow, buf, L"������", MB_OK);
		
	}
	shutdown(s, 2);
	closesocket(serverSocket);
	WSACleanup();
}

//����, �������������� ������� �������
void clientThread(void* param)
{
	clientData cd = *(clientData*)param;
	SOCKET s = cd.s;
	Integral* ig = 0;
	Integral* lastig = 0;

	int nread = 0;
	double resp = 0;
	bool isOk = 1;

	//���� �� ������� ��� ������ � ������� 10 ������, �������� ���
	DWORD timeout = 10000;

	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(DWORD)) != 0)
	{
		return;
	}
	//�������� ��������� ��������
	ig = &getNextIg();
	//���� ��� ��� ����, ������ ������� ���������, �������� �������
	if (ig->step == 0)
	{
		closesocket(s);
		deleteClientFromListBox(cd.addr);
		return;
	}
	lastig = ig;
	//���������� ��� ��������
	if (send(s, (char*)ig, sizeof(Integral), 0) < 0)
	{
		isOk = 0;
	}

	if (isOk)
	{

		while (1)
		{
			//�������� �����
			int nbytes = recv(s, (char*)&resp, sizeof(double), 0);
			if (nbytes <= 0)
			{
				//���� ���������� ����������, ���� ������
				isOk = 0;
				break;
			}
			//������� ��� �������� � ����������
			addResSafe(resp);
			//����� ��������� ��������
			ig = &getNextIg();
			//���� ��� ��� ����, ��������� ���������
			if (ig->step == 0)
			{

				ig->step = 0;
				send(s, (char*)ig, sizeof(Integral), 0);
				closesocket(s);
				closesocket(serverSocket);

				break;
			}

			lastig = ig;
			int snd = 0;
			//���������� ��������� �������, ��������� �� ����� �������
			snd = send(s, (char*)ig, sizeof(Integral), 0);
			if (snd <= 0)
			{
				isOk = 0;
				break;
			}
		}
	}
	if (!isOk)
	{
		//���� ��� �� ����������, ��������� ������ ������� � �������
		addLastIg(lastig);
	}
	closesocket(s);
	deleteClientFromListBox(cd.addr);
}
//������������� ����������
void setComplete(int perc, int sec)
{

	TCHAR outBuff[64] = { 0 };
	wsprintf(outBuff, L"%d%%, %d ���", perc, sec);

	SendMessage(hLabelPercentComplete, WM_SETTEXT, 0, (LPARAM)outBuff);
}



//���������� ���������
LRESULT CALLBACK windowsProcedure(HWND Window, UINT message, WPARAM wParam, LPARAM lParam) {
	int id = LOWORD(wParam), event = HIWORD(wParam);
	PAINTSTRUCT ps;

	HDC hdc;
	switch (message) {
	case WM_CTLCOLORSTATIC:
	{
		//���� ������������ ����������, �������� ��� ����� � ��������� ����
		HDC hdcStatic = (HDC)wParam;


		//����� ������
		SetTextColor(hdcStatic, RGB(0, 0, 0));
		SetBkColor(hdcStatic, RGB(255, 255, 255));
		return (INT_PTR)CreateSolidBrush(RGB(255, 255, 255));
	}

	case WM_COMMAND:
		switch (id) {
			//��������� ���������, ������� ������
		case C_COLOR:
			RedrawWindow(hWindow, 0, 0, RDW_INVALIDATE | RDW_ERASE);
			break;
			//������� �����
		case BTN_PRESS:
			if (!running)
			{
				preMain();
			}
			else
			{
				stopMain();
			}
			break;
		}
		break;
	case WM_PAINT:
		hdc = BeginPaint(Window, &ps);
		EndPaint(Window, &ps);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(Window, message, wParam, lParam);
	}
	return 0;
}




int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int cmdShow) {
	//������� �����
	srand(time(0));
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = 0;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hIcon = 0;
	wcex.hbrBackground = (HBRUSH)COLOR_WINDOWFRAME;
	wcex.lpszMenuName = 0;
	wcex.hIconSm = 0;
	wcex.lpfnWndProc = windowsProcedure;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.lpszClassName = L"Server";
	//������������ ���
	RegisterClassEx(&wcex);

	//������� �������� ���������
	hWindow = CreateWindow(L"Server", L"Server", WS_SYSMENU, 0, 0, 420, 180, NULL, NULL, hInstance, NULL);
	CreateWindowEx(0, L"Static", L"������:", WS_VISIBLE | WS_CHILD, 4, 4, 60, 16, hWindow, (HMENU)0, hInstance, 0);
	hLabelPercentComplete = CreateWindowEx(0, L"Static", L"--%, -- sec", WS_VISIBLE | WS_CHILD, 64, 4, 180, 16, hWindow, (HMENU)0, hInstance, 0);

	CreateWindowEx(0, L"Static", L"��:", WS_VISIBLE | WS_CHILD | WS_TABSTOP, 4, 24, 30, 16, hWindow, (HMENU)0, hInstance, 0);
	fromTB = CreateWindowEx(0, L"Edit", L"1", WS_VISIBLE | WS_CHILD | WS_BORDER, 44, 24, 130, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"��:", WS_VISIBLE | WS_CHILD, 4, 44, 30, 16, hWindow, (HMENU)0, hInstance, 0);
	toTB = CreateWindowEx(0, L"Edit", L"100000", WS_VISIBLE | WS_CHILD | WS_BORDER, 44, 44, 130, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"���:", WS_VISIBLE | WS_CHILD, 4, 64, 30, 16, hWindow, (HMENU)0, hInstance, 0);
	stepTB = CreateWindowEx(0, L"Edit", L"0.001", WS_VISIBLE | WS_CHILD | WS_BORDER, 44, 64, 130, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"����. �������� �������: ", WS_VISIBLE | WS_CHILD, 4, 84, 180, 16, hWindow, (HMENU)0, hInstance, 0);
	limTB = CreateWindowEx(0, L"Edit", L"1", WS_VISIBLE | WS_CHILD | WS_BORDER, 200, 84, 25, 16, hWindow, (HMENU)0, hInstance, 0);


	hClientListBox = CreateWindowEx(0, L"ListBox", L"LBox", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOVSCROLL | LBS_HASSTRINGS, 244, 4, 150, 142, hWindow, (HMENU)0, hInstance, 0);


	hBtn = CreateWindowEx(0, L"Button", L"�����", WS_VISIBLE | WS_CHILD | WS_BORDER, 4, 116, 220, 20, hWindow, (HMENU)BTN_PRESS, hInstance, 0);


	ShowWindow(hWindow, cmdShow);
	UpdateWindow(hWindow);

	//������������� ����������
	InitializeCriticalSection(&listBoxCriticalSection);
	InitializeCriticalSection(&queRetreiveCriticalSection);

	//�������� ���� ���������
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}
