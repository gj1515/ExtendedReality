#include <stdio.h>
#include <conio.h>
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include "windows.h"
#include "MvCameraControl.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")

bool g_bExit = false;
unsigned int g_nPayloadSize = 0;

// Recording
bool g_bRecording = false;
cv::VideoWriter g_videoWriter;
std::string g_outputPath = "D:/XR Lab/ExtendedReality/videos/";

std::atomic <bool> g_bToggleRecording(false);

unsigned int _stdcall TerminalInputThread(void* pParam)
{
    char ch;
    while (!g_bExit)
    {
        if (_kbhit())
        {
            ch = _getch();
            if (ch == 's' || ch == 'S')
            {
                g_bToggleRecording = true;
            }
            else if (ch == 'q' || ch == 'Q')
            {
                g_bExit = true;
            }
        }
        Sleep(100);
    }
    return 0;
}

void WaitForKeyPress(void)
{
    while (!_kbhit())
    {
        Sleep(10);
    }
    _getch();
}

std::string GetCurrentTimeString()
{
    time_t now = time(0);
    struct tm timeinfo;
    char buffer[80];
    localtime_s(&timeinfo, &now);
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &timeinfo);
    return std::string(buffer);
}


bool PrintDeviceInfo(MV_CC_DEVICE_INFO* pstMVDevInfo)
{
    if (NULL == pstMVDevInfo)
    {
        printf("The Pointer of pstMVDevInfo is NULL!\n");
        return false;
    }
    if (pstMVDevInfo->nTLayerType == MV_GIGE_DEVICE)
    {
        int nIp1 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0xff000000) >> 24);
        int nIp2 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x00ff0000) >> 16);
        int nIp3 = ((pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x0000ff00) >> 8);
        int nIp4 = (pstMVDevInfo->SpecialInfo.stGigEInfo.nCurrentIp & 0x000000ff);
        printf("CurrentIp: %d.%d.%d.%d\n", nIp1, nIp2, nIp3, nIp4);
        printf("UserDefinedName: %s\n\n", pstMVDevInfo->SpecialInfo.stGigEInfo.chUserDefinedName);
    }
    else if (pstMVDevInfo->nTLayerType == MV_USB_DEVICE)
    {
        printf("UserDefinedName: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chUserDefinedName);
        printf("Serial Number: %s\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
        printf("Device Number: %d\n\n", pstMVDevInfo->SpecialInfo.stUsb3VInfo.nDeviceNumber);
    }
    else
    {
        printf("Not support.\n");
    }

    return true;
}


int ResetCameraFrameCounter(void* handle)
{
    if (handle == NULL)
    {
        printf("Camera handle is NULL\n");
        return -1;
    }

    int nRet = MV_CC_SetCommandValue(handle, "CounterReset");

    nRet = MV_CC_StopGrabbing(handle);
    if (MV_OK != nRet)
    {
        printf("Stop Grabbing fail! nRet [0x%x]\n", nRet);
        return -1;
    }

    // Short delay to ensure camera has time to stop
    Sleep(100);

    nRet = MV_CC_StartGrabbing(handle);
    if (MV_OK != nRet)
    {
        printf("Restart Grabbing fail! nRet [0x%x]\n", nRet);
        return -1;
    }
    return 0;
}


void StartRecording(int width, int height)
{
    if (!g_bRecording)
    {
        CreateDirectoryA(g_outputPath.c_str(), NULL);

        std::string filename = g_outputPath + "recording_" + GetCurrentTimeString() + ".mp4";

        g_videoWriter.open(filename, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), 30, cv::Size(width, height), true);

        if (g_videoWriter.isOpened())
        {
            g_bRecording = true;
            printf("Recording started: %s\n", filename.c_str());
        }
        else
        {
            printf("Failed to create video file!\n");
        }
    }
}

void StopRecording()
{
    if (g_bRecording)
    {
        g_videoWriter.release();
        g_bRecording = false;
        printf("Recording stopped.\n");
    }
}


static  unsigned int __stdcall WorkThread(void* pUser)
{
    int nRet = MV_OK;
    MV_FRAME_OUT_INFO_EX stImageInfo = { 0 };
    unsigned char* pData = (unsigned char*)malloc(sizeof(unsigned char) * (g_nPayloadSize));
    if (pData == NULL)
    {
        return 0;
    }
    unsigned int nDataSize = g_nPayloadSize;

    cv::namedWindow("Camera Display", cv::WINDOW_NORMAL);
    cv::resizeWindow("Camera Display", 1280, 720);

    bool wasRecording = false;

    while (1)
    {
        if (g_bToggleRecording)
        {
            if (!g_bRecording)
            {
                cv::Size frameSize(0, 0);
                if (pData != NULL && stImageInfo.nWidth > 0 && stImageInfo.nHeight > 0)
                {
                    frameSize = cv::Size(stImageInfo.nWidth, stImageInfo.nHeight);
                }
                else
                {
                    frameSize = cv::Size(1280, 720);
                }

                StartRecording(frameSize.width, frameSize.height);
            }
            else
            {
                StopRecording();
            }

            g_bToggleRecording = false;
        }

        if (g_bRecording && !wasRecording)
        {
            ResetCameraFrameCounter(pUser);
            wasRecording = true;
        }
        else if (!g_bRecording && wasRecording)
        {
            wasRecording = false;
        }

        nRet = MV_CC_GetOneFrameTimeout(pUser, pData, nDataSize, &stImageInfo, 1000);
        if (nRet == MV_OK)
        {
            cv::Mat frame;
            cv::Mat rawFrame(stImageInfo.nHeight, stImageInfo.nWidth, CV_8UC1, pData);
            cv::cvtColor(rawFrame, frame, cv::COLOR_BayerRG2RGB);

            char text[64];
            sprintf_s(text, "Frame: %d", stImageInfo.nFrameNum);
            cv::putText(frame, text, cv::Point(20, 100), cv::FONT_HERSHEY_SIMPLEX, 3, cv::Scalar(255, 255, 255), 2);

            if (g_bRecording)
            {
                cv::circle(frame, cv::Point(2200, 100), 20, cv::Scalar(0, 0, 255), -1);
                cv::putText(frame, "REC", cv::Point(2240, 116), cv::FONT_HERSHEY_SIMPLEX, 1.5, cv::Scalar(0, 0, 255), 2);

                // Write frame
                g_videoWriter.write(frame);
            }

            cv::imshow("Camera Display", frame);

            int key = cv::waitKey(1);
            if (key == 27)
            {
                g_bExit = true;
                break;
            }
        }

        else
        {
            printf("No data[0x%x]\n", nRet);
        }

        if (g_bExit)
        {
            break;
        }
    }

    cv::destroyAllWindows();
    free(pData);
    return 0;
}

int main()
{
    int nRet = MV_OK;
    void* handle = NULL;
    do
    {
        // Camera Device
        MV_CC_DEVICE_INFO_LIST stDeviceList = { 0 };
        nRet = MV_CC_EnumDevices(MV_GIGE_DEVICE | MV_USB_DEVICE, &stDeviceList);
        if (MV_OK != nRet)
        {
            printf("Enum Devices fail! nRet [0x%x]\n", nRet);
            break;
        }

        if (stDeviceList.nDeviceNum > 0)
        {
            for (unsigned int i = 0; i < stDeviceList.nDeviceNum; i++)
            {
                printf("[device %d]:\n", i);
                MV_CC_DEVICE_INFO* pDeviceInfo = stDeviceList.pDeviceInfo[i];
                if (NULL == pDeviceInfo)
                {
                    break;
                }
                PrintDeviceInfo(pDeviceInfo);
            }
        }
        else
        {
            printf("Find No Devices!\n");
            break;
        }

        printf("Please Input camera index:");
        unsigned int nIndex = 0;
        scanf_s("%d", &nIndex);

        if (nIndex >= stDeviceList.nDeviceNum)
        {
            printf("Input error!\n");
            break;
        }

        // Camera handle
        nRet = MV_CC_CreateHandle(&handle, stDeviceList.pDeviceInfo[nIndex]);
        if (MV_OK != nRet)
        {
            printf("Create Handle fail! nRet [0x%x]\n", nRet);
            break;
        }

        // Open camera
        nRet = MV_CC_OpenDevice(handle);
        if (MV_OK != nRet)
        {
            printf("Open Device fail! nRet [0x%x]\n", nRet);
            break;
        }

        if (stDeviceList.pDeviceInfo[nIndex]->nTLayerType == MV_GIGE_DEVICE)
        {
            int nPacketSize = MV_CC_GetOptimalPacketSize(handle);
            if (nPacketSize > 0)
            {
                nRet = MV_CC_SetIntValue(handle, "GevSCPSPacketSize", nPacketSize);
                if (nRet != MV_OK)
                {
                    printf("Warning: Set Packet Size fail nRet [0x%x]!", nRet);
                }
            }
            else
            {
                printf("Warning: Get Packet Size fail nRet [0x%x]!", nPacketSize);
            }
        }
        int nPacketSize = MV_CC_GetOptimalPacketSize(handle);
        if (nPacketSize > 0)
        {
            nRet = MV_CC_SetIntValue(handle, "GevSCPSPacketSize", nPacketSize);
            if (nRet != MV_OK)
            {
                printf("Warning: Set Packet Size fail nRet [0x%x]!", nRet);
            }
        }
        else
        {
            printf("Warning: Get Packet Size fail nRet [0x%x]!", nPacketSize);
        }

        nRet = MV_CC_SetEnumValue(handle, "TriggerMode", 0);
        if (MV_OK != nRet)
        {
            printf("Set Trigger Mode fail! nRet [0x%x]\n", nRet);
            break;
        }

        MVCC_INTVALUE stParam = { 0 };
        nRet = MV_CC_GetIntValue(handle, "PayloadSize", &stParam);
        if (MV_OK != nRet)
        {
            printf("Get PayloadSize fail! nRet [0x%x]\n", nRet);
            break;
        }
        g_nPayloadSize = stParam.nCurValue;

        nRet = MV_CC_StartGrabbing(handle);
        if (MV_OK != nRet)
        {
            printf("Start Grabbing fail! nRet [0x%x]\n", nRet);
            break;
        }

        unsigned int nThreadID = 0;
        void* hThreadHandle = (void*)_beginthreadex(NULL, 0, WorkThread, handle, 0, &nThreadID);
        if (NULL == hThreadHandle)
        {
            break;
        }

        unsigned int nTerminalThreadID = 0;
        void* hTerminalThreadHandle = (void*)_beginthreadex(NULL, 0, TerminalInputThread, NULL, 0, &nTerminalThreadID);
        if (NULL == hTerminalThreadHandle)
        {
            printf("Failed to create terminal input thread\n");
            g_bExit = true;
            break;
        }

        printf("========= Camera Control Commands =========\n");
        printf("In terminal:\n");
        printf("  's' key: Start/Stop recording\n");
        printf("  'q' key: Quit application\n");
        printf("==========================================\n");

        while (!g_bExit)
        {
            Sleep(100);
        }
        
        WaitForSingleObject(hTerminalThreadHandle, INFINITE);
        CloseHandle(hTerminalThreadHandle);


        WaitForSingleObject(hThreadHandle, INFINITE);
        CloseHandle(hThreadHandle);
        
        if (g_bRecording)
        {
            StopRecording();
        }


        nRet = MV_CC_StopGrabbing(handle);
        if (MV_OK != nRet)
        {
            printf("Stop Grabbing fail! nRet [0x%x]\n", nRet);
            break;
        }

        nRet = MV_CC_CloseDevice(handle);
        if (MV_OK != nRet)
        {
            printf("ClosDevice fail! nRet [0x%x]\n", nRet);
            break;
        }

        nRet = MV_CC_DestroyHandle(handle);
        if (MV_OK != nRet)
        {
            printf("Destroy Handle fail! nRet [0x%x]\n", nRet);
            break;
        }
    } while (0);

    if (nRet != MV_OK)
    {
        if (handle != NULL)
        {
            MV_CC_DestroyHandle(handle);
            handle = NULL;
        }
    }

    printf("Press a key to exit.\n");
    WaitForKeyPress();
    return 0;
 }