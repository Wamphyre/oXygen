#include "ModuleRack.h"
#include "../PluginProcessor.h"
#include "../MasteringModule.h"
#include "ModuleWrapper.h"

using namespace oxygen;

ModuleRack::ModuleRack(OxygenAudioProcessor& p) : audioProcessor(p)
{
    contentComponent = std::make_unique<juce::Component>();
    setViewedComponent(contentComponent.get(), false);
    
    // Auto-show scrollbars only if needed, otherwise hidden
    setScrollBarsShown(true, false, false, true); // (vertical, horizontal, showVerticalHeader, showHorizontalHeader) -> wait, Viewport API is (bool vertical, bool horizontal)
    // Actually Viewport doesn't have auto-show easily exposed in some versions, but let's try standard behavior.
    // The user said "NO QUEREMOS SCROLL" (we don't want scroll). 
    // If the window is big enough, it won't scroll.
    setScrollBarsShown(false, false); // Let's try disabling it initially, or rely on auto-behavior if we don't force it.
    // Better: setScrollBarsShown(true, false) is what it was.
    // If we simply resize the container to be large enough, scrollbars won't appear.
    // BUT user said "WITHOUT THAT INFERNAL SCROLL". 
    // Let's keep it enabled but ensure sizing is correct so it doesn't trigger. 
    // Actually, maybe they specifically hate the visual scrollbar. 
    // Let's set it to false and ensure resizing works.
    setScrollBarsShown(false, false);
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
                auto* wrapper = new oxygen::ModuleWrapper(name, std::unique_ptr<juce::AudioProcessorEditor>(processor->createEditor()), bypassParam);
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
    
    int y = 0;
    int width = getWidth() - (getVerticalScrollBar().isVisible() ? getVerticalScrollBar().getWidth() : 0);
    
    for (auto* child : contentComponent->getChildren())
    {
        child->setBounds(0, y, width, child->getHeight());
        y += child->getHeight() + 5;
    }
    
    contentComponent->setSize(width, y);
}
