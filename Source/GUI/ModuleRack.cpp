#include "ModuleRack.h"
#include "../PluginProcessor.h"
#include "../MasteringModule.h"
#include "ModuleWrapper.h"

using namespace oxygen;

ModuleRack::ModuleRack(OxygenAudioProcessor& p) : audioProcessor(p)
{
    contentComponent = std::make_unique<juce::Component>();
    setViewedComponent(contentComponent.get(), false);

    // Show the vertical scrollbar only when the rack is taller than the viewport.
    setScrollBarsShown(true, false);
}

ModuleRack::~ModuleRack()
{
}

void ModuleRack::updateModuleList()
{
    moduleWrappers.clear();
    contentComponent->removeAllChildren();
    
    const auto& nodes = audioProcessor.getModuleNodes();
    
    for (int i = 0; i < (int)nodes.size(); ++i)
    {
        auto* node = nodes[i].get();
        if (auto* processor = node->getProcessor())
        {
            auto name = processor->getName();
            juce::RangedAudioParameter* bypassParam = nullptr;
            
            if (auto* mm = dynamic_cast<oxygen::MasteringModule*>(processor))
                bypassParam = mm->getBypassParameter();
            
            if (processor->hasEditor())
            {
                auto* wrapper = new oxygen::ModuleWrapper(name,
                                                          std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor()),
                                                          bypassParam,
                                                          processor);
                moduleWrappers.add(wrapper);
                contentComponent->addAndMakeVisible(wrapper);
                
                wrapper->onHeightChanged = [this] { resized(); };
                
                // Use a copy of i to avoid scope issues in lambda
                wrapper->onMoveUp   = [this, i] { audioProcessor.moveModuleUp(i);   updateModuleList(); };
                wrapper->onMoveDown = [this, i] { audioProcessor.moveModuleDown(i); updateModuleList(); };
            }
        }
    }
    
    resized();
}

void ModuleRack::resized()
{
    juce::Viewport::resized();

    int contentHeight = 0;
    for (auto* child : contentComponent->getChildren())
        contentHeight += child->getHeight() + 5;

    if (contentHeight > 0)
        contentHeight -= 5;

    const bool needsVerticalScroll = contentHeight > getMaximumVisibleHeight();
    const int contentWidth = juce::jmax(1, getWidth() - (needsVerticalScroll ? getScrollBarThickness() : 0));

    int y = 0;
    for (auto* child : contentComponent->getChildren())
    {
        child->setBounds(0, y, contentWidth, child->getHeight());
        y += child->getHeight() + 5;
    }

    contentComponent->setSize(contentWidth, juce::jmax(1, contentHeight));
}
