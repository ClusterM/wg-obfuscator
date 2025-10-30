// Custom JavaScript for WireGuard Obfuscator LuCI app

(function() {
    'use strict';
    
    function fixAddButton() {
        var addBtns = document.querySelectorAll('.cbi-button-add, input[value="Add"], .cbi-section-create input[type="submit"]');
        
        addBtns.forEach(function(btn) {
            if ((btn.value === 'Add' || btn.textContent === 'Add') && btn.value !== 'Add instance') {
                btn.value = 'Add instance';
                if (btn.textContent) btn.textContent = 'Add instance';
            }
        });
    }
    
    // Run on load
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', fixAddButton);
    } else {
        fixAddButton();
    }
    
    // Run after delay to catch dynamically loaded content
    setTimeout(fixAddButton, 100);
})();

