/*=========================================================================

  Program:   ParaView
  Module:    vtkPVKeyFrame.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "vtkPVKeyFrame.h"

#include "vtkSMKeyFrameProxy.h"
#include "vtkObjectFactory.h"
#include "vtkSMObject.h"
#include "vtkSMProxyManager.h"
#include "vtkSMBooleanDomain.h"
#include "vtkSMEnumerationDomain.h"
#include "vtkSMDoubleRangeDomain.h"
#include "vtkSMIntRangeDomain.h"
#include "vtkPVSelectionList.h"
#include "vtkKWLabel.h"
#include "vtkKWThumbWheel.h"
#include "vtkKWCheckButton.h"
#include "vtkKWEntry.h"
#include "vtkCommand.h"
#include "vtkPVApplication.h"
#include "vtkPVWindow.h"
#include "vtkPVAnimationManager.h"
#include "vtkPVAnimationScene.h"
#include "vtkPVAnimationCue.h"
#include "vtkSMAnimationCueProxy.h"
#include "vtkSMArrayListDomain.h"
#include "vtkSMStringListDomain.h"
#include "vtkSMIntVectorProperty.h"
#include "vtkSMDoubleVectorProperty.h"
#include "vtkSMIdTypeVectorProperty.h"
#include "vtkKWPushButton.h"

vtkCxxRevisionMacro(vtkPVKeyFrame, "1.3");
//*****************************************************************************
class vtkPVKeyFrameObserver : public vtkCommand
{
public:
  static vtkPVKeyFrameObserver* New()
    {
    return new vtkPVKeyFrameObserver;
    }
  void SetTarget(vtkPVKeyFrame* t)
    {
    this->Target = t;
    }
  virtual void Execute(vtkObject* obj, unsigned long event, void* calldata)
    {
    if (this->Target)
      {
      this->Target->ExecuteEvent(obj, event, calldata);
      }
    }
protected:
  vtkPVKeyFrameObserver()
    {
    this->Target = NULL;
    }
  vtkPVKeyFrame* Target;
};
//*****************************************************************************

//-----------------------------------------------------------------------------
vtkPVKeyFrame::vtkPVKeyFrame()
{
  this->Observer = vtkPVKeyFrameObserver::New();
  this->Observer->SetTarget(this);
  this->KeyFrameProxy = NULL;
  this->KeyFrameProxyName = NULL;
  this->KeyFrameProxyXMLName = NULL;
  this->TimeLabel = vtkKWLabel::New();
  this->TimeThumbWheel = vtkKWThumbWheel::New();
  this->ValueLabel = vtkKWLabel::New();
  this->TimeBounds[0] = -0.1;
  this->TimeBounds[1] = 1.1;
  this->ValueWidget = NULL;
  this->AnimationCue = NULL;
  this->Name = NULL;
  this->MinButton = vtkKWPushButton::New();
  this->MaxButton = vtkKWPushButton::New();
}

//-----------------------------------------------------------------------------
vtkPVKeyFrame::~vtkPVKeyFrame()
{
  this->Observer->SetTarget(NULL);
  this->Observer->Delete();
  if (this->KeyFrameProxyName)
    {
    vtkSMObject::GetProxyManager()->UnRegisterProxy(
      "animation_keyframes", this->KeyFrameProxyName);
    this->SetKeyFrameProxyName(0);
    }

  if (this->KeyFrameProxy)
    {
    this->KeyFrameProxy->Delete();
    this->KeyFrameProxy = NULL;
    }

  if (this->ValueWidget) 
    {
    this->ValueWidget->Delete();
    this->ValueWidget = NULL;
    }
  this->SetAnimationCue(NULL);
  this->SetKeyFrameProxyXMLName(0);

  this->TimeLabel->Delete();
  this->ValueLabel->Delete();
  this->TimeThumbWheel->Delete();
  this->MinButton->Delete();
  this->MaxButton->Delete();
  this->SetName(NULL);
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::Create(vtkKWApplication* app, const char* args)
{
  if (!this->KeyFrameProxyXMLName)
    {
    vtkErrorMacro("KeyFrameProxyXMLName must be set before calling Create");
    return;
    }

  if (!this->AnimationCue)
    {
    vtkErrorMacro("AnimationCue must be set before calling Create");
    return;
    }
  
  if (!this->Superclass::Create(app, "frame", args))
    {
    vtkErrorMacro("Failed to create the widget");
    return;
    }

  vtkSMProxyManager* pxm = vtkSMObject::GetProxyManager();
  static int proxyNum = 0;

  this->KeyFrameProxy = vtkSMKeyFrameProxy::SafeDownCast(
    pxm->NewProxy("animation_keyframes", this->KeyFrameProxyXMLName));

  if (!this->KeyFrameProxy)
    {
    vtkErrorMacro("Failed to create proxy " << this->KeyFrameProxyXMLName);
    return;
    }
  ostrstream str;
  str << "vtkPVKeyFrame_" << this->KeyFrameProxyXMLName << proxyNum << ends;
  this->SetKeyFrameProxyName(str.str());
  str.rdbuf()->freeze(0);
  proxyNum++;

  pxm->RegisterProxy("animation_keyframes", this->KeyFrameProxyName,
    this->KeyFrameProxy);
  
  this->KeyFrameProxy->AddObserver(vtkCommand::ModifiedEvent, this->Observer);
  this->KeyFrameProxy->UpdateVTKObjects(); // creates the proxy.
  this->ChildCreate(app);
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::ChildCreate(vtkKWApplication* app)
{
  this->TimeLabel->SetParent(this);
  this->TimeLabel->Create(app, 0);
  this->TimeLabel->SetLabel("Time:");
  
  this->TimeThumbWheel->SetParent(this);
  this->TimeThumbWheel->PopupModeOn();
  this->TimeThumbWheel->SetValue(0.0);
  this->TimeThumbWheel->SetResolution(0.01);
  this->TimeThumbWheel->Create(app, NULL);
  this->TimeThumbWheel->DisplayEntryOn();
  this->TimeThumbWheel->DisplayLabelOff();
  this->TimeThumbWheel->DisplayEntryAndLabelOnTopOff();
  this->TimeThumbWheel->ExpandEntryOn();
  this->TimeThumbWheel->SetEntryCommand(this, "TimeChangedCallback");
  this->TimeThumbWheel->SetEndCommand(this, "TimeChangedCallback");

  this->ValueLabel->SetParent(this);
  this->ValueLabel->Create(app, 0);
  this->ValueLabel->SetLabel("Value:");
  this->CreateValueWidget();

  this->MinButton->SetParent(this);
  this->MinButton->Create(this->GetApplication(),0);
  this->MinButton->SetLabel("min");
  this->MinButton->SetBalloonHelpString("Set the value to the minimum possible, given the "
    "current state of the system.");
  this->MinButton->SetCommand(this,"MinimumCallback");
  this->MaxButton->SetParent(this);
  this->MaxButton->Create(this->GetApplication(),0);
  this->MaxButton->SetLabel("max");
  this->MaxButton->SetBalloonHelpString("Set the value to the maximum possible, given the "
    "current state of the system.");
  this->MaxButton->SetCommand(this, "MaximumCallback");

  this->Script("grid %s %s x x x -sticky w",
    this->TimeLabel->GetWidgetName(),
    this->TimeThumbWheel->GetWidgetName());
  if (this->ValueWidget)
    {
    this->Script("grid %s %s x x x -sticky w",
      this->ValueLabel->GetWidgetName(),
      this->ValueWidget->GetWidgetName());
    }

  this->Script("grid columnconfigure %s 0 -weight 0",
    this->GetWidgetName());
  this->Script("grid columnconfigure %s 1 -weight 0",
    this->GetWidgetName());
  this->Script("grid columnconfigure %s 2 -weight 0",
    this->GetWidgetName());
  this->Script("grid columnconfigure %s 3 -weight 0",
    this->GetWidgetName());
  this->Script("grid columnconfigure %s 4 -weight 2",
    this->GetWidgetName());
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::CreateValueWidget()
{
  vtkSMAnimationCueProxy* cueProxy = this->AnimationCue->GetCueProxy();
  vtkSMDomain* domain = cueProxy->GetAnimatedDomain();
  if (!domain)
    {
    //vtkErrorMacro("Animated domain not specified!");
    //don't create a value widget.
    return;
    }
  
  vtkSMBooleanDomain* bd = vtkSMBooleanDomain::SafeDownCast(domain);
  vtkSMEnumerationDomain* ed = vtkSMEnumerationDomain::SafeDownCast(domain);
  vtkSMStringListDomain* sld = vtkSMStringListDomain::SafeDownCast(domain);

  if (ed)
    {
    vtkPVSelectionList* pvList = vtkPVSelectionList::New();
    pvList->SetParent(this);
    pvList->SetLabelVisibility(0);
    for (unsigned int cc=0; cc < ed->GetNumberOfEntries(); cc++)
      {
      const char* text = ed->GetEntryText(cc);
      int value = ed->GetEntryValue(cc);
      pvList->AddItem(text, value);
      }
    pvList->Create(this->GetApplication());
    pvList->SetModifiedCommand(this->GetTclName(), "ValueChangedCallback");
    this->ValueWidget = pvList;
    }
  else if (bd)
    {
    vtkKWCheckButton* kwCB = vtkKWCheckButton::New();
    kwCB->SetParent(this);
    kwCB->Create(this->GetApplication(), 0);
    kwCB->SetCommand(this, "ValueChangedCallback");
    this->ValueWidget = kwCB;
    }
  else if (sld) // also works for ArrayListDomain.
    {
    vtkPVSelectionList* pvList = vtkPVSelectionList::New();
    pvList->SetParent(this);
    pvList->SetLabelVisibility(0);
    for (unsigned int cc=0; cc < sld->GetNumberOfStrings(); cc++)
      {
      pvList->AddItem(sld->GetString(cc), cc);
      }
    pvList->Create(this->GetApplication());
    pvList->SetModifiedCommand(this->GetTclName(), "ValueChangedCallback");
    this->ValueWidget = pvList;
    }
  else 
    {
    vtkKWThumbWheel* pvWheel = vtkKWThumbWheel::New();
    pvWheel->SetParent(this);
    pvWheel->PopupModeOn();
    pvWheel->SetValue(0.0);
    if (vtkSMIntVectorProperty::SafeDownCast(cueProxy->GetAnimatedProperty()))
      {
      pvWheel->SetResolution(1);
      }
    else
      {
      pvWheel->SetResolution(0.01);
      }
    pvWheel->Create(this->GetApplication(), 0);
    pvWheel->DisplayEntryOn();
    pvWheel->DisplayLabelOff();
    pvWheel->DisplayEntryAndLabelOnTopOff();
    pvWheel->ExpandEntryOn();
    pvWheel->SetEntryCommand(this, "ValueChangedCallback");
    pvWheel->SetEndCommand(this, "ValueChangedCallback");
    this->ValueWidget = pvWheel;
    }
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::MinimumCallback()
{
  this->InitializeKeyValueDomainUsingCurrentState();
  vtkKWThumbWheel* pvWheel = vtkKWThumbWheel::SafeDownCast(this->ValueWidget);
  if (!pvWheel || !pvWheel->GetClampMinimumValue())
    {
    return;
    }
  this->SetKeyValue(pvWheel->GetMinimumValue());
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::MaximumCallback()
{
  this->InitializeKeyValueDomainUsingCurrentState();
  vtkKWThumbWheel* pvWheel = vtkKWThumbWheel::SafeDownCast(this->ValueWidget);
  if (!pvWheel || !pvWheel->GetClampMaximumValue())
    {
    return;
    }
  this->SetKeyValue(pvWheel->GetMaximumValue());
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::InitializeKeyValueUsingCurrentState()
{
  if (!this->ValueWidget)
    {
    return;
    }
  vtkSMAnimationCueProxy* cueProxy = this->AnimationCue->GetCueProxy();
  vtkSMProperty* property = cueProxy->GetAnimatedProperty();
  int index = cueProxy->GetAnimatedElement();
  if (vtkSMDoubleVectorProperty::SafeDownCast(property))
    {
    this->SetKeyValue(vtkSMDoubleVectorProperty::SafeDownCast(property)->GetElement(index));
    }
  else if (vtkSMIntVectorProperty::SafeDownCast(property))
    {
    this->SetKeyValue(static_cast<double>(vtkSMIntVectorProperty::SafeDownCast(
          property)->GetElement(index)));
    }
  else if (vtkSMIdTypeVectorProperty::SafeDownCast(property))
    {
    this->SetKeyValue(static_cast<double>(vtkSMIdTypeVectorProperty::SafeDownCast(
          property)->GetElement(index)));
    }
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::InitializeKeyValueDomainUsingCurrentState()
{
  if (!this->ValueWidget)
    {
    return;
    }
  vtkSMAnimationCueProxy* cueProxy = this->AnimationCue->GetCueProxy();
  int index = cueProxy->GetAnimatedElement();
  vtkSMDomain* domain = cueProxy->GetAnimatedDomain();
  vtkSMBooleanDomain* bd = vtkSMBooleanDomain::SafeDownCast(domain);
  vtkSMDoubleRangeDomain* drd = vtkSMDoubleRangeDomain::SafeDownCast(domain);
  vtkSMEnumerationDomain* ed = vtkSMEnumerationDomain::SafeDownCast(domain);
  vtkSMIntRangeDomain* ird = vtkSMIntRangeDomain::SafeDownCast(domain);
  if (bd || ed)
    {
    //no domain initialization necessary.
    }
  else if (drd)
    {
    // set range min and max values and clamp the thumbwidget.
    vtkKWThumbWheel* wheel = vtkKWThumbWheel::SafeDownCast(this->ValueWidget);
    if (!wheel)
      {
      vtkErrorMacro("Widget and domain mismatch!");
      return;
      }
    int exists = 0;
    double min;
    double max;
    min = drd->GetMinimum(index, exists);
    int col = 2;
    if (exists)
      {
      wheel->SetMinimumValue(min);
      wheel->ClampMinimumValueOn();
      this->Script("grid %s -column %d -row 1", 
        this->MinButton->GetWidgetName(), col);
      col++;
      }
    else
      {
      wheel->ClampMinimumValueOff();
      this->Script("grid forget %s", this->MinButton->GetWidgetName());
      }
    
    max = drd->GetMaximum(index, exists);
    if (exists)
      {
      wheel->SetMaximumValue(max);
      wheel->ClampMaximumValueOn();
      this->Script("grid %s -column %d -row 1", 
        this->MaxButton->GetWidgetName(), col);
      }
    else
      {
      wheel->ClampMaximumValueOff();
      this->Script("grid forget %s", this->MaxButton->GetWidgetName());
      }
    }
  else if (ird)
    {
    // set range min and max values and clamp the thumbwidget.
    vtkKWThumbWheel* wheel = vtkKWThumbWheel::SafeDownCast(this->ValueWidget);
    if (!wheel)
      {
      vtkErrorMacro("Widget and domain mismatch!");
      return;
      }
    int exists = 0;
    int min;
    int max;
    int col = 2;
    min = ird->GetMinimum(index, exists);
    if (exists)
      {
      wheel->SetMinimumValue(min);
      wheel->ClampMinimumValueOn();
      this->Script("grid %s -column %d -row 1", 
        this->MinButton->GetWidgetName(), col);
      col++;
      }
    else
      {
      wheel->ClampMinimumValueOff();
      this->Script("grid forget %s", this->MinButton->GetWidgetName());
      }
    max = ird->GetMaximum(index, exists);
    if (exists)
      {
      wheel->SetMaximumValue(max);
      wheel->ClampMaximumValueOn();
      this->Script("grid %s -column %d -row 1", 
        this->MaxButton->GetWidgetName(), col);
      }
    else
      {
      wheel->ClampMaximumValueOff();
      this->Script("grid forget %s", this->MaxButton->GetWidgetName());
      }
    } 
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::PrepareForDisplay()
{
  this->UpdateValuesFromProxy();
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::ValueChangedCallback()
{
  if (vtkPVSelectionList::SafeDownCast(this->ValueWidget))
    {
    this->SetKeyValue(static_cast<double>(
      vtkPVSelectionList::SafeDownCast(this->ValueWidget)->GetCurrentValue()));
    }
  else if (vtkKWCheckButton::SafeDownCast(this->ValueWidget))
    {
    this->SetKeyValue(static_cast<double>(
        vtkKWCheckButton::SafeDownCast(this->ValueWidget)->GetState()));
    }
  else if (vtkKWThumbWheel::SafeDownCast(this->ValueWidget))
    {
    this->SetKeyValue(
      vtkKWThumbWheel::SafeDownCast(this->ValueWidget)->GetEntry()->
      GetValueAsFloat());
    }
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::UpdateValuesFromProxy()
{
  double keyvalue = this->KeyFrameProxy->GetKeyValue();
  if (vtkPVSelectionList::SafeDownCast(this->ValueWidget))
    {
    vtkPVSelectionList::SafeDownCast(this->ValueWidget)->SetCurrentValue(
      static_cast<int>(keyvalue));
    }
  else if (vtkKWCheckButton::SafeDownCast(this->ValueWidget))
    {
    vtkKWCheckButton::SafeDownCast(this->ValueWidget)->SetState(
      static_cast<int>(keyvalue));
    }
  else if (vtkKWThumbWheel::SafeDownCast(this->ValueWidget))
    {
    vtkKWThumbWheel::SafeDownCast(this->ValueWidget)->SetValue(
      keyvalue);
    }

  this->TimeThumbWheel->SetValue(
    this->GetRelativeTime(this->KeyFrameProxy->GetKeyTime()));
}
//-----------------------------------------------------------------------------
void vtkPVKeyFrame::TimeChangedCallback()
{
  double ntime = this->GetNormalizedTime(
    this->TimeThumbWheel->GetEntry()->GetValueAsFloat());

  if (ntime < 0 || ntime <= this->TimeBounds[0] ||
    ntime > 1 || ntime >= this->TimeBounds[1])
    {
    // invalid change.
    this->TimeThumbWheel->SetValue(
      this->GetRelativeTime(this->KeyFrameProxy->GetKeyTime()));
    }
  else
    {
    this->SetKeyTime(ntime);
    }
}

//-----------------------------------------------------------------------------
double vtkPVKeyFrame::GetNormalizedTime(double rtime)
{
  vtkPVApplication* pvApp = vtkPVApplication::SafeDownCast(this->GetApplication());
  double duration = pvApp->GetMainWindow()->GetAnimationManager()->
    GetAnimationScene()->GetDuration();
  if (duration == 0)
    {
    vtkErrorMacro("Scene durantion is 0");
    return 0;
    }
  return rtime / duration;
}

//-----------------------------------------------------------------------------
double vtkPVKeyFrame::GetRelativeTime(double ntime)
{
  vtkPVApplication* pvApp = vtkPVApplication::SafeDownCast(this->GetApplication());
  double duration = pvApp->GetMainWindow()->GetAnimationManager()->
    GetAnimationScene()->GetDuration();
  return duration*ntime;
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::ExecuteEvent(vtkObject* , unsigned long event, void* )
{
  switch(event)
    {
  case vtkCommand::ModifiedEvent:
    this->UpdateValuesFromProxy();
    break;
    }
}



//-----------------------------------------------------------------------------
void vtkPVKeyFrame::ClearTimeBounds()
{
  this->TimeBounds[0] = -0.1;
  this->TimeBounds[1] = 1.1;
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::SetTimeMinimumBound(double min)
{
  this->TimeBounds[0] = min;
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::SetTimeMaximumBound(double max)
{
  this->TimeBounds[1] = max;
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::SetKeyTime(double time)
{
  this->KeyFrameProxy->SetKeyTime(time);
  this->AddTraceEntry("$kw(%s) SetKeyTime %f", this->GetTclName(), time);
}

//-----------------------------------------------------------------------------
double vtkPVKeyFrame::GetKeyTime()
{
  return this->KeyFrameProxy->GetKeyTime();
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::SetKeyValue(double val)
{
  this->KeyFrameProxy->SetKeyValue(val);
  this->AddTraceEntry("$kw(%s) SetKeyValue %f", this->GetTclName(), val);
}

//-----------------------------------------------------------------------------
double vtkPVKeyFrame::GetKeyValue()
{
  return this->KeyFrameProxy->GetKeyValue();
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::UpdateEnableState()
{
  this->Superclass::UpdateEnableState();
  if (this->ValueWidget)
    {
    this->PropagateEnableState(this->ValueWidget);
    }
  this->PropagateEnableState(this->TimeThumbWheel);
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::SaveState(ofstream* file)
{
  *file << "#State of a Key Frame " <<endl;
  *file << "$kw(" << this->GetTclName() << ") SetKeyValue "
    << this->GetKeyValue() << endl;
  *file << "$kw(" << this->GetTclName() << ") SetKeyTime "
    << this->GetKeyTime() << endl;
}

//-----------------------------------------------------------------------------
void vtkPVKeyFrame::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Name: " << ((this->Name)? this->Name : NULL) << endl;
  os << indent << "KeyFrameProxyXMLName: " << (this->KeyFrameProxyXMLName?
    this->KeyFrameProxyXMLName : "NULL") << endl;
  os << indent << "KeyFrameProxyName: " << (this->KeyFrameProxyName?
    this->KeyFrameProxyName : "NULL") << endl;
  os << indent << "KeyFrameProxy: " << this->KeyFrameProxy << endl;
  os << indent << "AnimationCue: " << this->AnimationCue << endl;
}

