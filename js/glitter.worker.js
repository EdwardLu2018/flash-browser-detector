import {GlitterModule} from './glitter-module';

onmessage = (e) => {
    const msg = e.data;
    switch (msg.type) {
        case 'init': {
            init(msg);
            return;
        }
        case 'process': {
            next = msg.imagedata;
            process();
            return;
        }
    }
}

var glitterModule = null;
var next = null;
var tags = null;

function init(msg) {
    function onLoaded() {
        postMessage({type: "loaded"});
    }

    glitterModule = new GlitterModule(
        msg.codes,
        msg.width,
        msg.height,
        msg.options,
        onLoaded
    );
}

function process() {
    if (glitterModule) {
        const start = Date.now();

        glitterModule.saveGrayscale(next);
        tags = glitterModule.detect_tags();
        postMessage({type: "result", tags: tags});

        const end = Date.now();

        if (glitterModule.options.printPerformance) {
            console.log("[performance]", "Detect:", end-start);
        }
    }
}
