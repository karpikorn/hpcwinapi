#include <Windows.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib, "ws2_32.lib")

//дескрипторы 
HWND hWindow;
//текстбоксы для выходных параметров
HWND ipTB, portTB, limTB, hBtn;

//критсекция записи резултата
CRITICAL_SECTION resCS;

HANDLE* sems = 0;



//пределы интегрирования и загрузки
double from, to, step, lim;
//результат вычислений
double result = 0;
//флаг вычислений
bool running;

sockaddr_in	saddrClient;

//хендлы тредов выполнения
HANDLE* workerThreads;
//хендл треда мерацния
HANDLE hColorThread = 0;
//хендл треда подключения
HANDLE hConnectionThread = 0;

//структура интеграла
typedef struct _Integral
{
	double from;
	double to;
	double step;
	double load;
} Integral;
typedef struct _params
{
	Integral* i;
	HANDLE sem;
} params;
#define C_COLOR 1
#define BTN_PRESS 2
//сокет подключения
SOCKET s;
//количество ядер в системе
int cpuCount;



//прототипы функций
void connectionThread(void* param);
void workerThread(void* param);
void startWork(char* ip, int port);
void calculationThread(void* param);
//подготовка к подключению
void preMain()
{
	from = -1;
	to = -1;
	step = -1;
	lim = -1;
	TCHAR wip[25] = { 0 };
	TCHAR wport[25] = { 0 };
	TCHAR wlim[25] = { 0 };

	GetWindowText(ipTB, wip, sizeof(wip));
	GetWindowText(portTB, wport, sizeof(wport));
	GetWindowText(limTB, wlim, sizeof(wlim));

	char buf[32];
	size_t len = wcstombs(buf, wip, wcslen(wip));
	if (len > 0u)
		buf[len] = '\0';

	puts(buf);


	if (wip[0] == 0 ||
		wport[0] == 0 ||
		wlim[0] == 0)
	{
		MessageBox(0, L"Один из параметров задан неверно или не задан", 0, 0);
		SetWindowText(hBtn, L"Connect");

		return;
	}

	lim = _wtof(wlim);
	int port = _wtoi(wport);

	startWork(buf,port);
}

//вычисления
void startWork(char* ip, int port)
{
	WSADATA wsd;
	if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
		MessageBox(hWindow, L"Ошибка загрузки Winsock", L"Ошибка", MB_OK);
		running = 0;
		SetWindowText(hBtn, L"Connect");

		return ;
	}

	if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == SOCKET_ERROR) {
		MessageBox(hWindow, L"Ошибка создания сокета", L"Ошибка", MB_OK);
		running = 0;
		SetWindowText(hBtn, L"Connect");

		return ;
	}
	SYSTEM_INFO siSysInfo;
	GetSystemInfo(&siSysInfo);
	cpuCount = siSysInfo.dwNumberOfProcessors;


	sems = (HANDLE*)malloc(sizeof(HANDLE)*cpuCount);
	ZeroMemory(sems, sizeof(HANDLE)*cpuCount);

	saddrClient.sin_addr.s_addr = inet_addr(ip);
	saddrClient.sin_port = htons(port);
	saddrClient.sin_family = AF_INET;
	//подключется к серверу
	if (connect(s, (sockaddr*)&saddrClient, sizeof(saddrClient)) == SOCKET_ERROR) {
		MessageBox(hWindow, L"Ошибка подключения", L"Ошибка", MB_OK);
		running = 0;
		closesocket(s);
		SetWindowText(hBtn, L"Connect");

		return;
	}

	
	hConnectionThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)connectionThread, &s, 0, 0);

}

Integral currentIntegral;
Integral* splitIntegrals = 0;

//тред, осуществляющий обмен данными с сервером
void connectionThread(void* param)
{
	SOCKET* s = (SOCKET*)param;

	if (splitIntegrals != 0)
	{
		free(splitIntegrals);
		
	}
	params* pars = (params*)malloc(sizeof(params) * cpuCount);
	ZeroMemory(pars, sizeof(params) * cpuCount);

	splitIntegrals = (Integral*)malloc(sizeof(Integral)*cpuCount);
	ZeroMemory(splitIntegrals, sizeof(Integral)*cpuCount);
	workerThreads = (HANDLE*)malloc(sizeof(HANDLE)*cpuCount);
	ZeroMemory(workerThreads, sizeof(HANDLE)*cpuCount);
	for (int i = 0; i < cpuCount; i++)
	{
		sems[i] = CreateSemaphore(0, 0, 1, 0);
		pars[i].sem = sems[i];
		pars[i].i = &splitIntegrals[i];
		workerThreads[i] = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)calculationThread, &pars[i], 0, 0);

		
	}
	Integral* ig = (Integral*)malloc(sizeof(Integral));


	ZeroMemory(ig, sizeof(Integral));
	while (1)
	{
		
		if (recv(*s, (char*)ig, sizeof(Integral), 0) < 0)
		{
			MessageBox(0, L"Ошибка при приеме данных", 0, 0);
			closesocket(*s);
			running = 0;
			SetWindowText(hBtn, L"Connect");

			return;
		}
		if (ig->step == 0)
		{
			closesocket(*s);
			running = 0;
			SetWindowText(hBtn, L"Connect");

			return;
		}
		

		//разбиваем полученный интеграл на куски для каждого процессора

		double lastfrom = ig->from, lastto = ig->to, step;
		double slice = (ig->to - ig->from) / cpuCount;
		for (int i = 0; i < cpuCount; i++)
		{
			splitIntegrals[i].from = lastfrom;
			splitIntegrals[i].to = lastfrom + slice;
			splitIntegrals[i].step = ig->step;
			splitIntegrals[i].load = ig->load;
			

			//params a = { 0 };

			pars[i].i = &splitIntegrals[i];
			ReleaseSemaphore(pars[i].sem, 1, 0);
			

			lastfrom += slice;
			//workerThreads[i] = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)calculationThread, &a, 0, 0);
		}
		//ждем завершения ниток вычислений
		WaitForMultipleObjects(cpuCount, sems, 1, INFINITE);
		//WaitForMultipleObjects(cpuCount, workerThreads, 1, INFINITE);
		//отправляем результат
		if (send(*s, (char*)&result, sizeof(double), 0) < 0)
		{
			MessageBox(0, L"Ошибка при передаче данных", 0, 0);
			closesocket(*s);
			running = 0;
			SetWindowText(hBtn, L"Connect");

			return;
		}
		result = 0;
	}
}

//здесь считается сам интеграл
void calculationThread(void* param)
{
	params* p = (params*)param;
	while (1)
	{
		Integral* ig = p->i;
		double res = 0;
		if (ig->step == 0)
			return;
		WaitForSingleObject(p->sem, INFINITE);
		//замеряем время выполнения вычислений

		double worktime = 0;
		double sleeptime = 0;
		int tact = 0;
		double actualpause = (double)lim;
		if (lim == 0)
		{
			lim = 1;
			actualpause = (double)lim;
			actualpause = ig->load;
		}
		actualpause /= (double)cpuCount;
		for (double i = ig->from; i < ig->to; i += ig->step)
		{
			tact = 0;
			double start = clock();
			while (tact < 2000000 && i < ig->to)
			{
				//метод срединных квадратов
				double stepadd = ig->step / (double)2;
				res += sqrt(i + stepadd);
				//double z = exp(res);
				tact++;
				i += ig->step;
			}
			double stop = clock();
			worktime += stop - start;

			start = clock();

			double rest = worktime * ((double)1 / lim) - worktime - sleeptime;
			if (rest >= 0)
			{
				Sleep(rest);
			}
			stop = clock();
			sleeptime += stop - start;
			//Sleep(1);
		}

		//если загрузка не была задана (0) используем загрузку с сервера (передается в структуре интеграла)

		//actualpause /= (double)cpuCount;



		//результат нужно домножить на шаг
		res *= ig->step;
		EnterCriticalSection(&resCS);
		//TCHAR buf[320];
		//swprintf(buf, L"%f, %f", workTime, rest);
		//if (rest >= 20) {
		//	SetWindowText(limTB, buf);
		//}
		//безопасно записываем в результат
		result += res;
		ReleaseSemaphore(p->sem, 1, 0);
		LeaveCriticalSection(&resCS);
	}
}

//обработчик оконных сообщений
LRESULT CALLBACK windowsProcedure(HWND Window, UINT message, WPARAM wParam, LPARAM lParam) {
	int id = LOWORD(wParam), event = HIWORD(wParam);
	PAINTSTRUCT ps;

	HDC hdc;
	switch (message) {
		
	case WM_CTLCOLORSTATIC:
	{
							  HDC hdcStatic = (HDC)wParam;

							  {
								  SetTextColor(hdcStatic, RGB(0, 0, 0));
								  SetBkColor(hdcStatic, RGB(255, 255, 255));
								  return (INT_PTR)CreateSolidBrush(RGB(255, 255, 255));
							  }
	}
		//см сервер
	case WM_COMMAND:
		switch (id) {
		case C_COLOR:
			RedrawWindow(hWindow, 0, 0, RDW_INVALIDATE | RDW_ERASE);
			break;
		case BTN_PRESS:
			if (!running)
			{
				//preMain();
				running = 1;
				
				SetWindowText(hBtn, L"Stop");

				preMain();
			}
			else
			{
				//stopMain();
				running = 0;
				closesocket(s);
				TerminateThread(hColorThread, 0);
				hColorThread = 0;
				SetWindowText(hBtn, L"Connect");

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
	//см сервер
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
	wcex.lpszClassName = L"Client";
	RegisterClassEx(&wcex);

	hWindow = CreateWindow(L"Client", L"Client", WS_SYSMENU, 0, 0, 420, 180, NULL, NULL, hInstance, NULL);

	CreateWindowEx(0, L"Static", L"IP:", WS_VISIBLE | WS_CHILD, 4, 4, 25, 16, hWindow, (HMENU)0, hInstance, 0);
	ipTB = CreateWindowEx(0, L"Edit", L"127.0.0.1", WS_VISIBLE | WS_CHILD | WS_BORDER, 70, 4, 325, 16, hWindow, (HMENU)0, hInstance, 0);

	CreateWindowEx(0, L"Static", L"Порт:", WS_VISIBLE | WS_CHILD, 4, 20, 35, 16, hWindow, (HMENU)0, hInstance, 0);
	portTB = CreateWindowEx(0, L"Edit", L"1316", WS_VISIBLE | WS_CHILD | WS_BORDER, 70, 20, 125, 16, hWindow, (HMENU)0, hInstance, 0);


	CreateWindowEx(0, L"Static", L"Предел загрузки (если ноль,", WS_VISIBLE | WS_CHILD, 4, 36, 240, 16, hWindow, (HMENU)0, hInstance, 0);
	CreateWindowEx(0, L"Static", L"используются значения с сервера): ", WS_VISIBLE | WS_CHILD, 4, 52, 340, 16, hWindow, (HMENU)0, hInstance, 0);
	limTB = CreateWindowEx(0, L"Edit", L"0", WS_VISIBLE | WS_CHILD | WS_BORDER, 250, 52, 350, 16, hWindow, (HMENU)0, hInstance, 0);



	hBtn = CreateWindowEx(0, L"Button", L"Connect", WS_VISIBLE | WS_CHILD, 4, 68, 65, 20, hWindow, (HMENU)BTN_PRESS, hInstance, 0);



	ShowWindow(hWindow, cmdShow);
	UpdateWindow(hWindow);

	InitializeCriticalSection(&resCS);
	MSG msg;
	while (GetMessage(&msg, 0, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}