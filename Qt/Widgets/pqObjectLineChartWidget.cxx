/*
 * Copyright 2004 Sandia Corporation.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the
 * U.S. Government. Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that this Notice and any
 * statement of authorship are reproduced on all copies.
 */

#include "pqObjectLineChartWidget.h"
#include "pqServer.h"

#include <pqLineChart.h>
#include <pqLineChartWidget.h>
#include <pqPiecewiseLine.h>
#include <pqLinearRamp.h>

#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkEventQtSlotConnect.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkSMClientSideDataProxy.h>
#include <vtkSMInputProperty.h>
#include <vtkSMProxyManager.h>
#include <vtkSMSourceProxy.h>
#include <vtkSphereSource.h>

#include <vtkExodusReader.h>
#include <vtkProcessModule.h>

//////////////////////////////////////////////////////////////////////////////
// pqObjectLineChartWidget::pqImplementation

struct pqObjectLineChartWidget::pqImplementation
{
  pqImplementation() :
    SourceProxy(0),
    ClientSideData(0),
    EventAdaptor(vtkEventQtSlotConnect::New()),
    CurrentVariable(""),
    CurrentElementID(0)
  {
  }
  
  ~pqImplementation()
  {
    this->EventAdaptor->Delete();
    
    if(this->ClientSideData)
      {
      this->ClientSideData->Delete();
      this->ClientSideData = 0;
      }
  }
  
  void setProxy(vtkSMSourceProxy* Proxy)
  {
    if(this->ClientSideData)
      {
      this->ClientSideData->Delete();
      this->ClientSideData = 0;
      }  

    this->SourceProxy = Proxy;

    this->CurrentVariable = QString();

    if(!Proxy)
      return;

    // Connect a client side data object to the input source
    this->ClientSideData = vtkSMClientSideDataProxy::SafeDownCast(
      Proxy->GetProxyManager()->NewProxy("displays", "ClientSideData"));
    vtkSMInputProperty* const input = vtkSMInputProperty::SafeDownCast(
      this->ClientSideData->GetProperty("Input"));
    input->AddProxy(Proxy);
    
    this->onInputChanged();
  }
  
  void setCurrentVariable(const QString& Variable)
  {
    this->CurrentVariable = Variable;
    this->updateChart();
  }
  
  void setCurrentElementID(unsigned long ElementID)
  {
    this->CurrentElementID = ElementID;
    this->updateChart();
  }
  
  void onInputChanged()
  {
    if(!this->ClientSideData)
      return;
    this->ClientSideData->UpdateVTKObjects();
    this->ClientSideData->Update();

    vtkDataSet* const data = this->ClientSideData->GetCollectedData();
    if(!data)
      return;

    // Populate the current-variable combo-box with available variables
    // (we do this here because the set of available variables may change as properties
    // are modified in e.g: a source or a reader)
    this->Variables.clear();
    this->Variables.addItem(QString(), QString());
    
    if(vtkPointData* const point_data = data->GetPointData())
      {
      for(int i = 0; i != point_data->GetNumberOfArrays(); ++i)
        {
        vtkDataArray* const array = point_data->GetArray(i);
        if(array->GetNumberOfComponents() != 1)
          continue;
        const char* const array_name = point_data->GetArrayName(i);
        this->Variables.addItem(QString(array_name) + " (point)", QString(array_name));
        }
      }
      
    if(vtkCellData* const cell_data = data->GetCellData())
      {
      for(int i = 0; i != cell_data->GetNumberOfArrays(); ++i)
        {
        vtkDataArray* const array = cell_data->GetArray(i);
        if(array->GetNumberOfComponents() != 1)
          continue;
        const char* const array_name = cell_data->GetArrayName(i);
        this->Variables.addItem(QString(array_name) + " (cell)", QString(array_name));
        }
      }

    this->updateChart();
  }
  
  void updateChart()
  {
    this->LineChartWidget.hide();
    
    if(!this->SourceProxy)
      return;

    const QString source_class = SourceProxy->GetVTKClassName();
    if(source_class != "vtkExodusReader" && source_class != "vtkPExodusReader")
      return;

    vtkProcessModule* const process_module = vtkProcessModule::GetProcessModule();
    vtkExodusReader* const reader = vtkExodusReader::SafeDownCast(
      process_module->GetObjectFromID(this->SourceProxy->GetID(0)));
    if(!reader)
      return;
    
    const int id = this->CurrentElementID + 1; // Exodus expects one-based cell ids
    QByteArray name = this->CurrentVariable.toAscii();
    const char* type = "CELL";
    vtkFloatArray* const array = vtkFloatArray::New();
    reader->GetTimeSeriesData(id, name.data(), type, array); 
    
    pqChartCoordinateList coordinates;
    for(vtkIdType i = 0; i != array->GetNumberOfTuples(); ++i)
      {
      double value = array->GetValue(i);
      coordinates.pushBack(pqChartCoordinate(static_cast<double>(i), value));
      }
      
    pqPiecewiseLine* const plot = new pqPiecewiseLine();
    plot->setCoordinates(coordinates);
    
    this->LineChartWidget.getLineChart()->setData(plot);
    this->LineChartWidget.show();

    array->Delete();
  }
  
  vtkSMSourceProxy* SourceProxy;
  vtkSMClientSideDataProxy* ClientSideData;
  vtkEventQtSlotConnect* EventAdaptor;
  QComboBox Variables;
  QSpinBox ElementID;
  pqLineChartWidget LineChartWidget;
  QString CurrentVariable;
  unsigned long CurrentElementID;
};

/////////////////////////////////////////////////////////////////////////////////
// pqObjectLineChartWidget

pqObjectLineChartWidget::pqObjectLineChartWidget(QWidget *parent) :
  QWidget(parent),
  Implementation(new pqImplementation())
{
  QLabel* const element_label = new QLabel(tr("Element:"));
  element_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  QHBoxLayout* const hbox = new QHBoxLayout();
  hbox->setMargin(0);
  hbox->addWidget(&this->Implementation->Variables, 4);
  hbox->addWidget(element_label);
  hbox->addWidget(&this->Implementation->ElementID, 1);

  QVBoxLayout* const vbox = new QVBoxLayout();
  vbox->setMargin(0);
  vbox->addLayout(hbox);
  vbox->addWidget(&this->Implementation->LineChartWidget);
  vbox->addStretch();
  this->setLayout(vbox);

  this->Implementation->ElementID.setMinimum(0);
  this->Implementation->ElementID.setMaximum(VTK_LONG_MAX);
  this->Implementation->ElementID.setValue(this->Implementation->CurrentElementID);

  QObject::connect(
    &this->Implementation->Variables,
    SIGNAL(activated(int)),
    this,
    SLOT(onCurrentVariableChanged(int)));
    
  QObject::connect(
    &this->Implementation->ElementID,
    SIGNAL(valueChanged(int)),
    this,
    SLOT(onElementIDChanged(int)));
}

pqObjectLineChartWidget::~pqObjectLineChartWidget()
{
  delete this->Implementation;
}

void pqObjectLineChartWidget::setProxy(vtkSMSourceProxy* proxy)
{
  this->Implementation->setProxy(proxy);
  
  if(proxy)
    {
    this->Implementation->EventAdaptor->Connect(
      proxy,
      vtkCommand::UpdateEvent,
      this,
      SLOT(onInputChanged(vtkObject*,unsigned long, void*, void*, vtkCommand*)));    
    }
}

void pqObjectLineChartWidget::setCurrentVariable(const QString& CurrentVariable)
{
  this->Implementation->setCurrentVariable(CurrentVariable);
}

void pqObjectLineChartWidget::setElementID(unsigned long ElementID)
{
  this->Implementation->setCurrentElementID(ElementID);
}

void pqObjectLineChartWidget::onInputChanged(vtkObject*,unsigned long, void*, void*, vtkCommand*)
{
  this->Implementation->onInputChanged();
}

void pqObjectLineChartWidget::onCurrentVariableChanged(int Row)
{
  this->Implementation->setCurrentVariable(this->Implementation->Variables.itemData(Row).toString());
}

void pqObjectLineChartWidget::onElementIDChanged(int ElementID)
{
  this->Implementation->setCurrentElementID(ElementID);
}
