#include "mod_input.h"

#include "menu_checker.h"

namespace vrinput
{
	using namespace vr;

	struct InputCallback
	{
		Hand              device;
		ActionType        type;
		InputCallbackFunc func;

		bool operator==(const InputCallback& a_rhs)
		{
			return (device == a_rhs.device) && (type == a_rhs.type) && (func == a_rhs.func);
		}
	};

	bool  block_all_inputs = false;
	bool  smoothing = 0;
	float joystick_dpad_threshold = 0.7f;
	float joystick_dpad_threshold_negative = -0.7f;
	float adjustable = 0.02f;

	std::mutex               callback_lock;
	vr::TrackedDeviceIndex_t g_leftcontroller;
	vr::TrackedDeviceIndex_t g_rightcontroller;

	std::deque<std::pair<ModInputEvent, vr::EVRButtonId>> fake_event_queue_left;
	std::deque<std::pair<ModInputEvent, vr::EVRButtonId>> fake_event_queue_right;

	vr::VRControllerAxis_t joystick[2] = {};
	float                  trigger[2];

	// each button id is mapped to a list of callback funcs
	std::unordered_map<int, std::vector<InputCallback>> callbacks;

	// I'm just going to store these the same way they come in
	std::array<std::array<uint64_t, 2>, 2> button_states = { { { 0ull, 0ull }, { 0ull, 0ull } } };

	std::vector<ModInputEvent> fake_button_states;

	void StartBlockingAll() { block_all_inputs = true; }
	void StopBlockingAll() { block_all_inputs = false; }
	bool isBlockingAll() { return block_all_inputs; }

	void StartSmoothing() { smoothing = 1; }
	void StopSmoothing() { smoothing = 0; }

	ButtonState GetButtonState(
		vr::EVRButtonId a_button_ID, Hand a_hand, ActionType a_touch_or_press)
	{
		return (ButtonState)((
			bool)(button_states[(int)a_hand][(int)a_touch_or_press] & 1ull << a_button_ID));
	}

	void AddCallback(const InputCallbackFunc a_callback, const vr::EVRButtonId a_button,
		const Hand hand, const ActionType touch_or_press)
	{
		std::scoped_lock lock(callback_lock);
		if (!a_callback) return;

		callbacks[a_button].push_back(InputCallback(hand, touch_or_press, a_callback));
	}

	void RemoveCallback(const InputCallbackFunc a_callback, const vr::EVRButtonId a_button,
		const Hand hand, const ActionType touch_or_press)
	{
		std::scoped_lock lock(callback_lock);
		if (!a_callback) return;

		auto it = std::find(callbacks[a_button].begin(), callbacks[a_button].end(),
			InputCallback(hand, touch_or_press, a_callback));
		if (it != callbacks[a_button].end()) { callbacks[a_button].erase(it); }
	}

	void AddHoldCallback(const InputCallbackFunc a_callback,
		const std::chrono::milliseconds a_duration, const vr::EVRButtonId a_button_ID,
		const Hand a_hand, const ActionType a_touch_or_press)
	{
		// TODO AddHoldCallback
	}

	void RemoveHoldCallback(const InputCallbackFunc a_callback, const vr::EVRButtonId a_button_ID,
		const Hand a_hand, const ActionType a_touch_or_press)
	{
		// TODO RemoveHoldCallback
	} 

	void SendFakeInputEvent(const ModInputEvent a_event)
	{
		if (a_event.device == Hand::kLeft)
		{
			fake_event_queue_left.push_back(std::make_pair(a_event, a_event.button_ID));
		}
		else { fake_event_queue_right.push_back(std::make_pair(a_event, a_event.button_ID)); }
	}

	void SetFakeButtonState(const ModInputEvent a_event) { fake_button_states.push_back(a_event); }

	void ClearFakeButtonState(const ModInputEvent a_event)
	{
		auto it = std::find(fake_button_states.begin(), fake_button_states.end(), a_event);
		if (it != fake_button_states.end()) { fake_button_states.erase(it); }
	}

	void ProcessButtonChanges(uint64_t changedMask, uint64_t currentState, bool isLeft, bool touch,
		vr::VRControllerState_t* out)
	{
		// update private button states
		button_states[isLeft][touch] = currentState;

		// iterate through each of the button codes that we care about
		for (auto buttonID : all_buttons)
		{
			uint64_t bitmask = 1ull << buttonID;

			// check if this button's state has changed and it has any callbacks
			if (bitmask & changedMask && callbacks.contains(buttonID))
			{
				// check whether it was a press or release event
				bool buttonPress = bitmask & currentState;

				const ModInputEvent event_flags = ModInputEvent(static_cast<Hand>(isLeft),
					static_cast<ActionType>(touch), static_cast<ButtonState>(buttonPress));

				// iterate through callbacks for this button and call if flags match
				for (auto& cb : callbacks[buttonID])
				{
					if (cb.device == event_flags.device && cb.type == event_flags.touch_or_press)
					{
						// the callback tells us if we should block the input
						if (cb.func(event_flags))
						{
							if (buttonPress)  // clear the current state of the button
							{
								if (touch) { out->ulButtonTouched &= ~bitmask; }
								else { out->ulButtonPressed &= ~bitmask; }
							}
							else  // set the current state of the button
							{
								if (touch) { out->ulButtonTouched |= bitmask; }
								else { out->ulButtonPressed |= bitmask; }
							}
						}
					}
				}
			}
		}
	}

	/* range: -1.0 to 1.0 for joystick, 0.0 to 1.0 for trigger ( 0 = not touching) */
	inline void ProcessAxisChanges(
		const VRControllerAxis_t& a_joystick, const float& a_trigger, bool isLeft)
	{
		static std::vector<bool> dpad_buffer[2] = { { false, false, false, false },
			{ false, false, false, false } };

		std::vector<bool> dpad_temp = {
			(a_joystick.x < joystick_dpad_threshold_negative),
			(a_joystick.y > joystick_dpad_threshold),
			(a_joystick.x > joystick_dpad_threshold),
			(a_joystick.y < joystick_dpad_threshold_negative),
		};

		if (dpad_buffer[isLeft] != dpad_temp)
		{
			for (int id = 0; id < dpad.size(); id++)
			{
				if (dpad_buffer[isLeft][id] != dpad_temp[id])
				{
					const ModInputEvent event_flags = ModInputEvent(static_cast<Hand>(isLeft),
						ActionType::kPress, static_cast<ButtonState>((bool)dpad_temp[id]));

					// iterate through callbacks for this button and call if flags match
					for (auto& cb : callbacks[id + (int)vr::k_EButton_DPad_Left])
					{
						if (cb.device == event_flags.device &&
							cb.type == event_flags.touch_or_press)
						{
							cb.func(event_flags);
						}
					}
				}
			}
			dpad_buffer[isLeft] = dpad_temp;
		}

		trigger[isLeft] = a_trigger;
		joystick[isLeft] = a_joystick;
	}

	/* For spoofing button presses. VRTools lets us clear bits but not set them.
	* see https://github.com/SkyrimAlternativeDevelopers/SkyrimVRTools/blob/master/src/hooks/HookVRSystem.cpp#L91
	* this code is effectively:
	* finalState.ulButtonPressed |= modifiedControllerState.ulButtonPressed;
	* finalState.ulButtonTouched |= modifiedControllerState.ulButtonTouched;
	*/
	struct AsmSetControllerState : Xbyak::CodeGenerator
	{
		AsmSetControllerState()
		{
			or_(r12, ptr[rbp - 0x68]);
			or_(r13, ptr[rbp - 0x60]);
			ret();
		}
	};

	struct AsmSetControllerAxes : Xbyak::CodeGenerator
	{
		AsmSetControllerAxes()
		{
			//TODO: check if other mods are overwriting, probably don't need to write to all of these

			// joystick.x (arg1)
			movss(ptr[rbp - 0x98], xmm0);
			movss(ptr[rbp - 0x58], xmm0);
			movss(ptr[rbp - 0x18], xmm0);
			// joystick.y (arg2)
			movss(ptr[rbp - 0x94], xmm1);
			movss(ptr[rbp - 0x54], xmm1);
			movss(ptr[rbp - 0x14], xmm1);
			movss(xmm15, xmm1);
			// trigger (arg3)
			movss(ptr[rbp - 0x90], xmm2);
			movss(ptr[rbp - 0x50], xmm2);
			movss(ptr[rbp - 0x10], xmm2);
			movss(xmm14, xmm2);

			ret();
		}
	};

	// handles low level button/trigger events
	bool ControllerInputCallback(vr::TrackedDeviceIndex_t unControllerDeviceIndex,
		const vr::VRControllerState_t* pControllerState, uint32_t unControllerStateSize,
		vr::VRControllerState_t* pOutputControllerState)
	{
		// save last controller input to only do processing on button changes
		static uint64_t prev_pressed[2] = {};
		static uint64_t prev_touched[2] = {};

		// need to remember the last output sent to the game in order to maintain input blocking
		static uint64_t prev_pressed_out[2] = {};
		static uint64_t prev_touched_out[2] = {};

		if (pControllerState && !menuchecker::isGameStopped())
		{
			bool isLeft = unControllerDeviceIndex == g_leftcontroller;
			if (isLeft || unControllerDeviceIndex == g_rightcontroller)
			{
				uint64_t pressed_change = prev_pressed[isLeft] ^ pControllerState->ulButtonPressed;
				uint64_t touched_change = prev_touched[isLeft] ^ pControllerState->ulButtonTouched;

				ProcessAxisChanges(
					pControllerState->rAxis[0], pControllerState->rAxis[1].x, isLeft);

				if (pressed_change)
				{
					ProcessButtonChanges(pressed_change, pControllerState->ulButtonPressed, isLeft,
						false, pOutputControllerState);
					prev_pressed[isLeft] = pControllerState->ulButtonPressed;
					prev_pressed_out[isLeft] = pOutputControllerState->ulButtonPressed;
				}
				else { pOutputControllerState->ulButtonPressed = prev_pressed_out[isLeft]; }

				if (touched_change)
				{
					ProcessButtonChanges(touched_change, pControllerState->ulButtonTouched, isLeft,
						true, pOutputControllerState);
					prev_touched[isLeft] = pControllerState->ulButtonTouched;
					prev_touched_out[isLeft] = pOutputControllerState->ulButtonTouched;
				}
				else { pOutputControllerState->ulButtonTouched = prev_touched_out[isLeft]; }

				if (isBlockingAll())
				{
					pOutputControllerState->ulButtonPressed = 0;
					pOutputControllerState->ulButtonTouched = 0;
					pOutputControllerState->rAxis->x = 0.0f;
					pOutputControllerState->rAxis->y = 0.0f;
				}

				auto local_trigger = pControllerState->rAxis[1].x;

				// momentary button spoofing
				if ((isLeft && !fake_event_queue_left.empty()) ||
					(!isLeft && !fake_event_queue_right.empty()))
				{
					static std::deque<std::pair<ModInputEvent, vr::EVRButtonId>>* spoof_queue;
					spoof_queue = isLeft ? &fake_event_queue_left : &fake_event_queue_right;

					do {
						auto&     event = spoof_queue->front();
						uint64_t* state = event.first.touch_or_press == ActionType::kPress ?
							&(pOutputControllerState->ulButtonPressed) :
							&(pOutputControllerState->ulButtonTouched);

						*state = event.first.button_state == ButtonState::kButtonDown ?
							*state | 1ull << event.second :
							*state & ~(1ull << event.second);

						if (event.second == k_EButton_SteamVR_Trigger)
						{
							if (event.first.button_state == ButtonState::kButtonDown &&
								event.first.touch_or_press == ActionType::kPress)
							{
								local_trigger = 1.f;
							}
							else { local_trigger = 0.f; }
						}

						spoof_queue->pop_front();

					} while (!spoof_queue->empty());
				}

				// hold button spoofing
				for (auto& event : fake_button_states)
				{
					if (isLeft == (bool)event.device)
					{
						uint64_t* state = event.touch_or_press == ActionType::kPress ?
							&(pOutputControllerState->ulButtonPressed) :
							&(pOutputControllerState->ulButtonTouched);

						*state = event.button_state == ButtonState::kButtonDown ?
							*state | 1ull << event.button_ID :
							*state & ~(1ull << event.button_ID);

						if (event.button_ID == k_EButton_SteamVR_Trigger &&
							event.touch_or_press == ActionType::kPress)
						{
							if (event.button_state == ButtonState::kButtonDown)
							{
								local_trigger = 1.f;
							}
							else { local_trigger = 0.f; }
						}
					}
				}

				// TODO: make these static?

				AsmSetControllerAxes code_set_axes;
				code_set_axes.ready();
				auto SetControllerAxes = code_set_axes.getCode<void (*)(float, float, float)>();
				SetControllerAxes(
					pControllerState->rAxis[0].x, pControllerState->rAxis[0].y, local_trigger);

				AsmSetControllerState code_set_buttons;
				code_set_buttons.ready();
				auto SetControllerStateFunc = code_set_buttons.getCode<void (*)(void)>();
				SetControllerStateFunc();
			}
		}
		return true;
	}

	RE::NiTransform HmdMatrixToNiTransform(const HmdMatrix34_t& hmdMatrix)
	{
		RE::NiTransform niTransform;

		// Copy rotation matrix
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j) { niTransform.rotate.entry[i][j] = hmdMatrix.m[i][j]; }
		}

		// Copy translation vector
		niTransform.translate.x = hmdMatrix.m[0][3];
		niTransform.translate.y = hmdMatrix.m[1][3];
		niTransform.translate.z = hmdMatrix.m[2][3];

		return niTransform;
	}

	HmdMatrix34_t NiTransformToHmdMatrix(const RE::NiTransform& niTransform)
	{
		HmdMatrix34_t hmdMatrix;

		// Copy rotation matrix
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j) { hmdMatrix.m[i][j] = niTransform.rotate.entry[i][j]; }
		}

		// Copy translation vector
		hmdMatrix.m[0][3] = niTransform.translate.x;
		hmdMatrix.m[1][3] = niTransform.translate.y;
		hmdMatrix.m[2][3] = niTransform.translate.z;

		return hmdMatrix;
	}

	// handles device poses
	vr::EVRCompositorError ControllerPoseCallback(VR_ARRAY_COUNT(unRenderPoseArrayCount)
													  vr::TrackedDevicePose_t* pRenderPoseArray,
		uint32_t                                                      unRenderPoseArrayCount,
		VR_ARRAY_COUNT(unGamePoseArrayCount) vr::TrackedDevicePose_t* pGamePoseArray,
		uint32_t                                                      unGamePoseArrayCount)
	{
		static vr::TrackedDevicePose_t simple_buffer[2] = {};

		for (auto isLeft : { true, false })
		{
			auto device = isLeft ? g_leftcontroller : g_rightcontroller;

			if (smoothing)
			{
				auto pre = HmdMatrixToNiTransform(simple_buffer[isLeft].mDeviceToAbsoluteTracking);
				auto cur = HmdMatrixToNiTransform(pGamePoseArray[device].mDeviceToAbsoluteTracking);

				// TODO: real filtering algorithm
				constexpr float low_rate = 0.04f;
				constexpr float mid_rate = 0.14f;
				constexpr float high_rate = 0.2f;
				constexpr float low_threshold = 0.003f * 0.003f;
				constexpr float mid_threshold = 0.008f * 0.008f;
				constexpr float high_threshold = 0.05f * 0.05f;

				auto dist = pre.translate.GetSquaredDistance(cur.translate);

				if (dist > high_threshold)
				{
					pre.translate = helper::LinearInterp(pre.translate, cur.translate, high_rate);
				}
				else if (dist > mid_threshold)
				{
					pre.translate = helper::LinearInterp(pre.translate, cur.translate, mid_rate);
				}
				else if (dist > low_threshold)
				{
					pre.translate = helper::LinearInterp(pre.translate, cur.translate, low_rate);
				}

				// adaptive slerp?
				pre.rotate = helper::slerpMatrixAdaptive(pre.rotate, cur.rotate);

				pGamePoseArray[device].mDeviceToAbsoluteTracking = NiTransformToHmdMatrix(pre);
				simple_buffer[isLeft] = pGamePoseArray[device];
			}
			else { simple_buffer[isLeft] = pGamePoseArray[device]; }
		}
		return vr::EVRCompositorError::VRCompositorError_None;
	}
}
