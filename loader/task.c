/*
 *        Tasks functions
 */
static char Copyright[] = "Copyright  Martin Ayotte, 1994";

/*
#define DEBUG_TASK
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "wine.h"
#include "task.h"

static LPTASKENTRY lpTaskList = NULL;
static int nTaskCount = 0;


/**********************************************************************
 *				GetCurrentTask	[KERNEL.36]
 */
HTASK GetCurrentTask()
{
	LPTASKENTRY lpTask = lpTaskList;
	int pid = getpid();
#ifdef DEBUG_TASK
	printf("GetCurrentTask() // unix_pid=%08X !\n", pid);
#endif
	if (lpTask == NULL) return 0;
	while (TRUE) {
		if (lpTask->unix_pid == pid) break;
		if (lpTask->lpNextTask == NULL) return 0;
		lpTask = lpTask->lpNextTask;
		}
#ifdef DEBUG_TASK
	printf("GetCurrentTask() returned hTask=%04X !\n", lpTask->hTask);
#endif
	return lpTask->hTask;
}


/**********************************************************************
 *				GetNumTasks	[KERNEL.152]
 */
WORD GetNumTasks()
{
	printf("GetNumTasks() returned %d !\n", nTaskCount);
	return nTaskCount;
}


/**********************************************************************
 *				GetWindowTask	[USER.224]
 */
HTASK GetWindowTask(HWND hWnd)
{
	HWND 	*wptr;
	int		count;
	LPTASKENTRY lpTask = lpTaskList;
	printf("GetWindowTask(%04X) !\n", hWnd);
	while (lpTask != NULL) {
		wptr = lpTask->lpWndList;
		if (wptr != NULL) {
			count = 0;
			while (++count < MAXWIN_PER_TASK) {
				printf("GetWindowTask // searching %04X %04X !\n",
										lpTask->hTask, *(wptr));
				if (*(wptr) == hWnd) {
					printf("GetWindowTask(%04X) found hTask=%04X !\n", 
												hWnd, lpTask->hTask);
					return lpTask->hTask;
					}
				wptr++;
				}
			}
		lpTask = lpTask->lpNextTask;
		}
	return 0;
}


/**********************************************************************
 *				EnumTaskWindows	[USER.225]
 */
BOOL EnumTaskWindows(HANDLE hTask, FARPROC lpEnumFunc, LONG lParam)
{
	HWND 	*wptr, hWnd;
	BOOL	bRet;
	int		count = 0;
	LPTASKENTRY lpTask = lpTaskList;
	printf("EnumTaskWindows(%04X, %08X, %08X) !\n", hTask, lpEnumFunc, lParam);
	while (TRUE) {
		if (lpTask->hTask == hTask) break;
		if (lpTask == NULL) {
			printf("EnumTaskWindows // hTask=%04X not found !\n", hTask);
			return FALSE;
			}
		lpTask = lpTask->lpNextTask;
		}
	printf("EnumTaskWindows // found hTask=%04X !\n", hTask);
	wptr = lpTask->lpWndList;
	if (wptr == NULL) return FALSE;
	while ((hWnd = *(wptr++)) != 0) {
		if (++count >= MAXWIN_PER_TASK) return FALSE;
		printf("EnumTaskWindows // hWnd=%04X count=%d !\n", hWnd, count);
#ifdef WINELIB
		if (lpEnumFunc != NULL)	bRet = (*lpEnumFunc)(hWnd, lParam); 
#else
		if (lpEnumFunc != NULL)	
			bRet = CallBack16(lpEnumFunc, 2, lParam, (int) hWnd);
#endif
		if (bRet == 0) break;
		}
	return TRUE;
}


/**********************************************************************
 *				CreateNewTask		[internal]
 */
HANDLE CreateNewTask(HINSTANCE hInst)
{
    HANDLE hTask;
	LPTASKENTRY lpTask = lpTaskList;
	LPTASKENTRY lpNewTask;
	if (lpTask != NULL) {
		while (TRUE) {
			if (lpTask->lpNextTask == NULL) break;
			lpTask = lpTask->lpNextTask;
			}
		}
	hTask = GlobalAlloc(GMEM_MOVEABLE, sizeof(TASKENTRY));
	lpNewTask = (LPTASKENTRY) GlobalLock(hTask);
#ifdef DEBUG_TASK
    printf("CreateNewTask entry allocated %08X\n", lpNewTask);
#endif
	if (lpNewTask == NULL) return 0;
	if (lpTaskList == NULL) {
		lpTaskList = lpNewTask;
		lpNewTask->lpPrevTask = NULL;
		}
	else {
		lpTask->lpNextTask = lpNewTask;
		lpNewTask->lpPrevTask = lpTask;
		}
	lpNewTask->lpNextTask = NULL;
	lpNewTask->hIcon = 0;
	lpNewTask->hModule = 0;
	lpNewTask->hInst = hInst;
	lpNewTask->hTask = hTask;
	lpNewTask->unix_pid = getpid();
	lpNewTask->lpWndList = (HWND *) malloc(MAXWIN_PER_TASK * sizeof(HWND));
	if (lpNewTask->lpWndList != NULL) 
		memset((LPSTR)lpNewTask->lpWndList, 0, MAXWIN_PER_TASK * sizeof(HWND));
#ifdef DEBUG_TASK
    printf("CreateNewTask // unix_pid=%08X return hTask=%04X\n", 
									lpNewTask->unix_pid, hTask);
#endif
	GlobalUnlock(hTask);	
	nTaskCount++;
    return hTask;
}


/**********************************************************************
 *				AddWindowToTask		[internal]
 */
BOOL AddWindowToTask(HTASK hTask, HWND hWnd)
{
	HWND 	*wptr;
	int		count = 0;
	LPTASKENTRY lpTask = lpTaskList;
#ifdef DEBUG_TASK
	printf("AddWindowToTask(%04X, %04X); !\n", hTask, hWnd);
#endif
	while (TRUE) {
		if (lpTask->hTask == hTask) break;
		if (lpTask == NULL) {
			printf("AddWindowToTask // hTask=%04X not found !\n", hTask);
			return FALSE;
			}
		lpTask = lpTask->lpNextTask;
		}
	wptr = lpTask->lpWndList;
	if (wptr == NULL) return FALSE;
	while (*(wptr) != 0) {
		if (++count >= MAXWIN_PER_TASK) return FALSE;
		wptr++;
		}
	*wptr = hWnd;
#ifdef DEBUG_TASK
	printf("AddWindowToTask // window added, count=%d !\n", count);
#endif
	return TRUE;
}


/**********************************************************************
 *				RemoveWindowFromTask		[internal]
 */
BOOL RemoveWindowFromTask(HTASK hTask, HWND hWnd)
{
	HWND 	*wptr;
	int		count = 0;
	LPTASKENTRY lpTask = lpTaskList;
#ifdef DEBUG_TASK
	printf("RemoveWindowToTask(%04X, %04X); !\n", hTask, hWnd);
#endif
	while (TRUE) {
		if (lpTask->hTask == hTask) break;
		if (lpTask == NULL) {
			printf("RemoveWindowFromTask // hTask=%04X not found !\n", hTask);
			return FALSE;
			}
		lpTask = lpTask->lpNextTask;
		}
	wptr = lpTask->lpWndList;
	if (wptr == NULL) return FALSE;
	while (*(wptr) != hWnd) {
		if (++count >= MAXWIN_PER_TASK) return FALSE;
		wptr++;
		}
	while (*(wptr) != 0) {
		*(wptr) = *(wptr + 1);
		if (++count >= MAXWIN_PER_TASK) return FALSE;
		wptr++;
		}
#ifdef DEBUG_TASK
	printf("RemoveWindowFromTask // window removed, count=%d !\n", --count);
#endif
	return TRUE;
}


