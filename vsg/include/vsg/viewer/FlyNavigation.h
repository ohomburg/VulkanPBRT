#pragma once

/* <editor-fold desc="MIT License">

Copyright(c) 2019 Robert Osfield

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

</editor-fold> */

#include <vsg/maths/transform.h>
#include <vsg/ui/ApplicationEvent.h>
#include <vsg/ui/KeyEvent.h>
#include <vsg/ui/PointerEvent.h>
#include <vsg/ui/ScrollWheelEvent.h>
#include <vsg/viewer/Camera.h>
#include <vsg/viewer/EllipsoidModel.h>

namespace vsg
{
    class VSG_DECLSPEC FlyNavigation : public Inherit<Visitor, FlyNavigation>
    {
    public:
        FlyNavigation(ref_ptr<Camera> camera);

        /// compute non dimensional window coordinate (-1,1) from event coords
        dvec2 ndc(PointerEvent& event);

        /// compute trackball coordinate from event coords
        dvec3 tbc(PointerEvent& event);

        void apply(KeyPressEvent& keyPress) override;
        void apply(KeyReleaseEvent& keyRelease) override;
        void apply(ButtonPressEvent& buttonPress) override;
        void apply(ButtonReleaseEvent& buttonRelease) override;
        void apply(MoveEvent& moveEvent) override;
        void apply(ScrollWheelEvent& scrollWheel) override;
        void apply(FrameEvent& frame) override;

        virtual void rotate(double angleUp, double angleRight);
        virtual void walk(dvec3 dir);

        bool withinRenderArea(int32_t x, int32_t y) const;

        /// add Key to Viewpoint binding using a LookAt to define the viewpoint
        void addKeyViewpoint(KeySymbol key, ref_ptr<LookAt> lookAt, double duration = 1.0);

        /// set the LookAt viewport to the specified lookAt, animating the movments from the current lookAt to the new one.
        /// A value of 0.0 instantly moves the lookAt to the new value.
        void setViewpoint(ref_ptr<LookAt> lookAt, double duration = 1.0);

        struct Viewpoint
        {
            ref_ptr<LookAt> lookAt;
            double duration = 0.0;
        };

        /// container that maps key symbol bindings with the Viewpoint that should move the LookAt to when pressed.
        std::map<KeySymbol, Viewpoint> keyViewpoitMap;

        /// Button mask value used to enable panning of the view, defaults to left mouse button
        ButtonMask rotateButtonMask = BUTTON_MASK_1;

        /// Button mask value used to enable panning of the view, defaults to middle mouse button
        ButtonMask panButtonMask = BUTTON_MASK_2;

        /// Button mask value used to enable zooming of the view, defaults to right mouse button
        ButtonMask zoomButtonMask = BUTTON_MASK_3;

        /// Scale for control how rapidly the view zooms in/out. Positive value zooms in when mouse moved downwards
        double zoomScale = 1.0;

    protected:
        ref_ptr<Camera> _camera;
        ref_ptr<LookAt> _lookAt;

        bool _hasFocus = false;
        bool _lastPointerEventWithinRenderArea = false;

        enum UpdateMode
        {
            INACTIVE = 0,
            MOVE
        };
        UpdateMode _updateMode = INACTIVE;
        double _zoomPreviousRatio = 0.0;
        dvec3 _walkDir{};

        time_point _previousTime;
        ref_ptr<PointerEvent> _previousPointerEvent;
        double _previousDelta = 0.0;
        bool _thrown = false;

        time_point _startTime;
        ref_ptr<LookAt> _startLookAt;
        ref_ptr<LookAt> _endLookAt;
        double _animationDuration = 0.0;
    };

} // namespace vsg
