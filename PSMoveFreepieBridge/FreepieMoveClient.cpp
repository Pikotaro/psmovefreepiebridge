/*
* Edited from test_console_client.cpp in PSMoveService
*/

#include "stdafx.h"
#include "../thirdparty/headers/FreePIE/freepie_io.h"
#include "../thirdparty/headers/glm/glm.hpp"
#include "../thirdparty/headers/glm/gtc/matrix_transform.hpp"
#include "../thirdparty/headers/glm/gtc/quaternion.hpp"
#include "../thirdparty/headers/glm/gtc/type_ptr.hpp"
#include "../thirdparty/headers/glm/gtx/euler_angles.hpp"
#include "FreepieMoveClient.h"

__declspec(dllexport) void WriteToFreepie(freepie_io_6dof_data data, int32_t freepieIndex = 0);

FreepieMoveClient::FreepieMoveClient()
{
}

int FreepieMoveClient::run(eDeviceType deviceType, int32_t deviceCount, int32_t deviceIDs[], PSMTrackingColorType bulbColors[], int32_t freepieIndicies[], bool sendSensorData)
{
	// Attempt to start and run the client
	try
	{
		m_device_type= deviceType;

		if (deviceType == _deviceTypeHMD)
		{
			trackedHmdIDs = deviceIDs;
			trackedHmdCount = deviceCount;
		}
		else
		{
			trackedControllerIDs = deviceIDs;
			trackedControllerCount = deviceCount;
		}

		trackedFreepieIndicies = freepieIndicies;
		trackedBulbColors = bulbColors;
		m_sendSensorData = sendSensorData;

		if (startup())
		{
			while (m_keepRunning)
			{
				update();

				Sleep(1);
			}
		}
		else
		{
			std::cerr << "Failed to startup the Freepie Move Client" << std::endl;
		}
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}

	// Attempt to shutdown the client
	try
	{
		shutdown();
	}
	catch (std::exception& e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}

void FreepieMoveClient::handle_client_psmove_event(PSMEventMessage::eEventType event_type)
{
	switch (event_type)
	{
	case PSMEventMessage::PSMEvent_connectedToService:
		std::cout << "FreepieMoveClient - Connected to service" << std::endl;
		if (m_device_type == _deviceTypeHMD)
		{
			init_hmd_views();
		}
		else
		{
			init_controller_views();
		}
		break;
	case PSMEventMessage::PSMEvent_failedToConnectToService:
		std::cout << "FreepieMoveClient - Failed to connect to service" << std::endl;
		m_keepRunning = false;
		break;
	case PSMEventMessage::PSMEvent_disconnectedFromService:
		std::cout << "FreepieMoveClient - Disconnected from service" << std::endl;
		m_keepRunning = false;
		break;
	case PSMEventMessage::PSMEvent_opaqueServiceEvent:
		std::cout << "FreepieMoveClient - Opaque service event(%d). Ignored." << static_cast<int>(event_type) << std::endl;
		break;
	case PSMEventMessage::PSMEvent_controllerListUpdated:
		if (m_device_type == _deviceTypeController)
		{
			std::cout << "FreepieMoveClient - Controller list updated. Reinitializing controller views." << std::endl;
			free_controller_views();
			init_controller_views();
		}
		else
		{
			std::cout << "FreepieMoveClient - Controller list updated. Ignored." << std::endl;
		}
		break;
	case PSMEventMessage::PSMEvent_trackerListUpdated:
		std::cout << "FreepieMoveClient - Tracker list updated. Ignored." << std::endl;
		break;
	case PSMEventMessage::PSMEvent_hmdListUpdated:        
		if (m_device_type == _deviceTypeHMD)
		{
			std::cout << "FreepieMoveClient - HMD list updated. Reinitializing HMD views." << std::endl;
			free_hmd_views();
			init_hmd_views();
		}
		else
		{
			std::cout << "FreepieMoveClient - HMD list updated. Ignored." << std::endl;
		}
		break;
	case PSMEventMessage::PSMEvent_systemButtonPressed:
		std::cout << "FreepieMoveClient - System button pressed. Ignored." << std::endl;
		break;
	default:
		std::cout << "FreepieMoveClient - unhandled event(%d)" << static_cast<int>(event_type) << std::endl;
		break;
	}
}

void FreepieMoveClient::handle_acquire_hmd(PSMResult resultCode, PSMHmdID trackedHmdIndex)
{
	if (resultCode == PSMResult_Success)
	{
		std::cout << "FreepieMoveClient - Acquired HMD "
			<< hmd_views[trackedHmdIndex]->HmdID << std::endl;
	}
	else
	{
		std::cout << "FreepieMoveClient - failed to acquire HMD " << std::endl;
		//m_keepRunning = false;
	}
}

void FreepieMoveClient::handle_acquire_controller(PSMResult resultCode, PSMControllerID trackedControllerIndex)
{
	if (resultCode == PSMResult_Success)
	{
		std::cout << "FreepieMoveClient - Acquired controller "
			<< controller_views[trackedControllerIndex]->ControllerID << std::endl;
	}
	else
	{
		std::cout << "FreepieMoveClient - failed to acquire controller " << std::endl;
		//m_keepRunning = false;
	}
}

bool FreepieMoveClient::startup()
{
	bool success = true;

	// Attempt to connect to the server
	if (success)
	{
		if (PSM_InitializeAsync("localhost", "9512") == PSMResult_Error)
		{
			std::cout << "FreepieMoveClient - Failed to initialize the client network manager" << std::endl;
			success = false;
		}
	}

	if (success)
	{
		last_report_fps_timestamp =
			std::chrono::duration_cast< std::chrono::milliseconds >(
				std::chrono::system_clock::now().time_since_epoch());
	}

	return success;
}

void FreepieMoveClient::update()
{
	// Process incoming/outgoing networking requests
	PSM_UpdateNoPollMessages();

	// Poll events queued up by the call to PSM_UpdateNoPollMessages()
	PSMMessage message;
	while (PSM_PollNextMessage(&message, sizeof(message)) == PSMResult_Success)
	{
		switch (message.payload_type)
		{
		case PSMMessage::_messagePayloadType_Response:
            {
                int trackedDeviceCount= (m_device_type == _deviceTypeHMD) ? trackedHmdCount : trackedControllerCount;

			    for (int i = 0; i < trackedDeviceCount; i++)
			    {
				    if (start_stream_request_ids[i] != -1 &&
					    message.response_data.request_id == start_stream_request_ids[i])
				    {
					    if (m_device_type == _deviceTypeHMD)
					    {
						    handle_acquire_hmd(message.response_data.result_code, i);
					    }
					    else
					    {
						    handle_acquire_controller(message.response_data.result_code, i);
					    }
					    start_stream_request_ids[i] = -1;
				    }
			    }
            }
			break;
		case PSMMessage::_messagePayloadType_Event:
			handle_client_psmove_event(message.event_data.event_type);
			break;
		}
	}

	//Button data must be outside of loop because it contains data for all tracked controllers!
	freepie_io_6dof_data buttonData;

	buttonData.pitch= 0.f;
	buttonData.roll= 0.f;
	buttonData.yaw= 0.f;
	buttonData.x= 0.f;
	buttonData.y= 0.f;
	buttonData.z= 0.f;

	if (m_device_type == _deviceTypeHMD)
	{
		for (int i = 0; i < trackedHmdCount; i++)
		{
			if (hmd_views[i] && hmd_views[i]->bValid)
			{
				std::chrono::milliseconds now =
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch());
				std::chrono::milliseconds diff = now - last_report_fps_timestamp;

				PSMPosef hmdPose;
				if (PSM_GetHmdPose(hmd_views[i]->HmdID, &hmdPose) == PSMResult_Success)
				{
					freepie_io_6dof_data poseData;

					poseData.x = hmdPose.Position.x;
					poseData.y = hmdPose.Position.y;
					poseData.z = hmdPose.Position.z;
				
					if (hmd_views[i]->HmdType == PSMHmd_Morpheus)
					{
						PSMQuatf normalizedQuat = PSM_QuatfNormalizeWithDefault(&hmdPose.Orientation, k_psm_quaternion_identity);

						//Calculate rotation here, glm library doesn't work for yaw
						//Both glm and this seem to work fine when each axis is independent, but issues when combined. 
						poseData.yaw = std::atan2(2 * normalizedQuat.y * normalizedQuat.w - 2 * normalizedQuat.x * normalizedQuat.z, 1 - 2 * normalizedQuat.y * normalizedQuat.y - 2 * normalizedQuat.z * normalizedQuat.z);
						poseData.roll = std::asin(2 * normalizedQuat.x * normalizedQuat.y + 2 * normalizedQuat.z * normalizedQuat.w);
						poseData.pitch = std::atan2(2 * normalizedQuat.x * normalizedQuat.w - 2 * normalizedQuat.y * normalizedQuat.z, 1 - 2 * normalizedQuat.x * normalizedQuat.x - 2 * normalizedQuat.z * normalizedQuat.z);

						WriteToFreepie(poseData, trackedFreepieIndicies[i]);

						if (m_sendSensorData)
						{
							PSMMorpheusCalibratedSensorData sensors = hmd_views[i]->HmdState.MorpheusState.CalibratedSensorData;

							//Send sensor data through pos/rot struct
							freepie_io_6dof_data sensorData1;
							sensorData1.x = sensors.Accelerometer.x;
							sensorData1.y = sensors.Accelerometer.y;
							sensorData1.z = sensors.Accelerometer.z;

							sensorData1.pitch = sensors.Gyroscope.x;
							sensorData1.roll = sensors.Gyroscope.y;
							sensorData1.yaw = sensors.Gyroscope.z;
							WriteToFreepie(sensorData1, 1);
						}
					}
					else
					{
						poseData.yaw= 0.f;
						poseData.roll= 0.f;
						poseData.pitch= 0.f;

						WriteToFreepie(poseData, trackedFreepieIndicies[i]);
					}
				}
			}
		}
	}
	else
	{
		for (int i = 0; i < trackedControllerCount; i++)
		{
			if (controller_views[i] && controller_views[i]->bValid && controller_views[i]->ControllerType == PSMController_Move)
			{
				std::chrono::milliseconds now =
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch());
				std::chrono::milliseconds diff = now - last_report_fps_timestamp;

				PSMPSMove moveView = controller_views[i]->ControllerState.PSMoveState;
				PSMPosef controllerPose = moveView.Pose;

				freepie_io_6dof_data poseData;
				PSMQuatf normalizedQuat = PSM_QuatfNormalizeWithDefault(&controllerPose.Orientation, k_psm_quaternion_identity);
				//glm::quat glmOrientation = glm::quat(normalizedQuat.w, normalizedQuat.x, normalizedQuat.y, normalizedQuat.z);

				poseData.x = controllerPose.Position.x;
				poseData.y = controllerPose.Position.y;
				poseData.z = controllerPose.Position.z;

				//data.pitch = glm::pitch(glmOrientation);
				//data.yaw = glm::yaw(glmOrientation);
				//data.roll = glm::roll(glmOrientation);

				//Calcuate rotation here, glm library doesn't work for yaw
				//Both glm and this seem to work fine when each axis is independent, but issues when combined. 
				poseData.yaw = std::atan2(2 * normalizedQuat.y * normalizedQuat.w - 2 * normalizedQuat.x * normalizedQuat.z, 1 - 2 * normalizedQuat.y * normalizedQuat.y - 2 * normalizedQuat.z * normalizedQuat.z);
				poseData.roll = std::asin(2 * normalizedQuat.x * normalizedQuat.y + 2 * normalizedQuat.z * normalizedQuat.w);
				poseData.pitch = std::atan2(2 * normalizedQuat.x * normalizedQuat.w - 2 * normalizedQuat.y * normalizedQuat.z, 1 - 2 * normalizedQuat.x * normalizedQuat.x - 2 * normalizedQuat.z * normalizedQuat.z);

				WriteToFreepie(poseData, trackedFreepieIndicies[i]);

				if (m_sendSensorData)
				{
					PSMPSMoveCalibratedSensorData sensors = moveView.CalibratedSensorData;

					//Send sensor data through pos/rot struct
					freepie_io_6dof_data sensorData1;
					sensorData1.x = sensors.Accelerometer.x;
					sensorData1.y = sensors.Accelerometer.y;
					sensorData1.z = sensors.Accelerometer.z;

					sensorData1.pitch = sensors.Gyroscope.x;
					sensorData1.roll = sensors.Gyroscope.y;
					sensorData1.yaw = sensors.Gyroscope.z;
					WriteToFreepie(sensorData1, 1);

					freepie_io_6dof_data sensorData2;
					sensorData2.x = sensors.Magnetometer.x;
					sensorData2.y = sensors.Magnetometer.y;
					sensorData2.z = sensors.Magnetometer.z;

					WriteToFreepie(sensorData2, 2);
				}

				// If we have less than four controllers, also include button data
				if (trackedControllerCount < 4)
				{
					float triggerState = static_cast<float>(moveView.TriggerValue) / 255.f;
					uint8_t buttonsPressed = 0;

					buttonsPressed |= (moveView.SquareButton == PSMButtonState_DOWN);
					buttonsPressed |= ((moveView.TriangleButton == PSMButtonState_DOWN) << 1);
					buttonsPressed |= ((moveView.CrossButton == PSMButtonState_DOWN) << 2);
					buttonsPressed |= ((moveView.CircleButton == PSMButtonState_DOWN) << 3);
					buttonsPressed |= ((moveView.MoveButton == PSMButtonState_DOWN) << 4);
					buttonsPressed |= ((moveView.PSButton == PSMButtonState_DOWN) << 5);
					buttonsPressed |= ((moveView.StartButton == PSMButtonState_DOWN) << 6);
					buttonsPressed |= ((moveView.SelectButton == PSMButtonState_DOWN) << 7);

					switch (i)
					{
						case 0:
							buttonData.x = buttonsPressed;
							buttonData.yaw = triggerState;
							break;
						case 1:
							buttonData.y = buttonsPressed;
							buttonData.pitch = triggerState;
							break;
						case 2:
							buttonData.z = buttonsPressed;
							buttonData.roll = triggerState;
							break;
						case 3:
							break;
						defaut:
							std::cout << "Unable to set button data for controller " << i << std::endl;
							break;
					}
				}
			}

			WriteToFreepie(buttonData, 3);
		}
	}
}

void FreepieMoveClient::shutdown()
{
	std::cout << "FreepieMoveClient is shutting down!" << std::endl;
	
	free_controller_views();
	free_hmd_views();

	// Close all active network connections
	PSM_Shutdown();
}

FreepieMoveClient::~FreepieMoveClient()
{
	shutdown();
}

void FreepieMoveClient::init_controller_views() {
	// Once created, updates will automatically get pushed into this view
	for (int i = 0; i < trackedControllerCount; i++)
	{
		PSM_AllocateControllerListener(trackedControllerIDs[i]);
		controller_views[i] = PSM_GetController(trackedControllerIDs[i]);

		// Kick off request to start streaming data from the first controller
		PSM_StartControllerDataStreamAsync(
			controller_views[i]->ControllerID, 
			m_sendSensorData ? PSMStreamFlags_includePositionData | PSMStreamFlags_includeCalibratedSensorData : PSMStreamFlags_includePositionData,
			&start_stream_request_ids[i]);

		//Set bulb color if specified
		if ((trackedBulbColors[i] >= 0) && (trackedBulbColors[i] < PSMTrackingColorType_MaxColorTypes)) {
			PSMRequestID request_id;
			PSM_SetControllerLEDColorAsync(controller_views[i]->ControllerID, trackedBulbColors[i], &request_id);
			PSM_EatResponse(request_id);
		}
	}
}

void FreepieMoveClient::free_controller_views() {
	// Free any allocated controller views
	for (int i = 0; i < trackedControllerCount; i++)
	{
		if (controller_views[i] != nullptr)
		{
			// Stop the controller stream
			PSMRequestID request_id;
			PSM_StopControllerDataStreamAsync(controller_views[i]->ControllerID, &request_id);
			PSM_EatResponse(request_id);

			// Free out controller listener
			PSM_FreeControllerListener(controller_views[i]->ControllerID);
			controller_views[i] = nullptr;
		}
	}
}


void FreepieMoveClient::init_hmd_views() {
	// Once created, updates will automatically get pushed into this view
	for (int i = 0; i < trackedHmdCount; i++)
	{
		PSM_AllocateHmdListener(trackedHmdIDs[i]);
		hmd_views[i] = PSM_GetHmd(trackedHmdIDs[i]);

		// Kick off request to start streaming data from the first hmd
		const bool bHasSensor= hmd_views[i]->HmdType == PSMHmd_Morpheus;
		PSM_StartHmdDataStreamAsync(
			hmd_views[i]->HmdID, 
			(bHasSensor && m_sendSensorData) 
			? PSMStreamFlags_includePositionData | PSMStreamFlags_includeCalibratedSensorData 
			: PSMStreamFlags_includePositionData,
			&start_stream_request_ids[i]);
	}
}

void FreepieMoveClient::free_hmd_views() {
	// Free any allocated hmd views
	for (int i = 0; i < trackedHmdCount; i++)
	{
		if (hmd_views[i] != nullptr)
		{
			// Stop the hmd stream
			PSMRequestID request_id;
			PSM_StopHmdDataStreamAsync(hmd_views[i]->HmdID, &request_id);
			PSM_EatResponse(request_id);

			// Free out controller listener
			PSM_FreeHmdListener(hmd_views[i]->HmdID);
			hmd_views[i] = nullptr;
		}
	}
}
